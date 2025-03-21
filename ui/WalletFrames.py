##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2024, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

import sys
import math

from armoryengine.BDM import TheBDM, BDM_BLOCKCHAIN_READY
from qtpy import QtCore, QtGui, QtWidgets
from qtdialogs.qtdefines import ArmoryFrame, VERTICAL, HORIZONTAL, \
   tightSizeNChar, makeHorizFrame, makeVertFrame, QRichLabel, \
   QPixMapButton, GETFONT, STYLE_SUNKEN, HLINE, determineWalletType, \
   QMoneyLabel, makeLayoutFrame, createToolTipWidget

from armorycolors import htmlColor
from armoryengine.ArmoryUtils import enum, isASCII, coin2str
from armoryengine.CppBridge import TheBridge


if sys.version_info < (3,0):
   import qrc_img_resources

WALLET_DATA_ENTRY_FIELD_WIDTH = 60

from qtdialogs.DlgUnlockWallet   import DlgUnlockWallet
from qtdialogs.DlgShowKeyList    import DlgShowKeyList
from qtdialogs.DlgRestore        import OpenPaperBackupDialog
from qtdialogs.qtdefines         import QRadioButtonBackupCtr, STRETCH, \
   MIN_PASSWD_WIDTH

from ui.CoinControlUI import CoinControlDlg, RBFDlg

class LockboxSelectFrame(ArmoryFrame):
   def __init__(self, parent, main, layoutDir=VERTICAL, spendFromLBID=None):
      super(LockboxSelectFrame, self).__init__(parent, main)

      self.lbox = self.main.getLockboxByID(spendFromLBID)
      self.cppWlt = self.main.cppLockboxWltMap[spendFromLBID]

      if not self.lbox:
         QtWidgets.QMessageBox.warning(self, self.tr("Invalid Lockbox"), self.tr(
            'There was an error loading the specified lockbox (%s).' % spendFromLBID),
         QtWidgets.QMessageBox.Ok)
         self.reject()
         return

      lblSpendFromLB = QRichLabel(self.tr('<font color="%s" size=4><b><u>Lockbox '
         '%s (%d-of-%d)</u></b></font>' % (htmlColor('TextBlue'), self.lbox.uniqueIDB58, self.lbox.M, self.lbox.N)))
      lblSpendFromLB.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      lbls = []
      lbls.append(QRichLabel(self.tr("Lockbox ID:"), doWrap=False))
      lbls.append(QRichLabel(self.tr("Name:"), doWrap=False))
      lbls.append(QRichLabel(self.tr("Description:"), doWrap=False))
      lbls.append(QRichLabel(self.tr("Spendable BTC:"), doWrap=False))

      layoutDetails = QtWidgets.QGridLayout()
      for i,lbl in enumerate(lbls):
         lbl.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)
         lbl.setText('<b>' + str(lbls[i].text()) + '</b>')
         layoutDetails.addWidget(lbl, i+1, 0)
         
      # LockboxID
      self.dispID = QRichLabel(spendFromLBID)

      # Lockbox Short Description/Name
      self.dispName = QRichLabel(self.lbox.shortName)
      self.dispName.setWordWrap(True)
      self.dispName.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Preferred)

      # Lockbox long descr
      dispDescr = self.lbox.longDescr[:253]
      if len(self.lbox.longDescr)>253:
         dispDescr += '...'
      self.dispDescr = QRichLabel(dispDescr)
      self.dispDescr.setWordWrap(True)
      self.dispDescr.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Preferred)
      bal = self.cppWlt.getSpendableBalance()
      self.dispBal = QMoneyLabel(bal, wBold=True)
      self.dispBal.setTextFormat(QtCore.Qt.RichText)

      layoutDetails.addWidget(self.dispID, 1, 1)
      layoutDetails.addWidget(self.dispName, 2, 1)
      layoutDetails.addWidget(self.dispDescr, 3, 1)
      layoutDetails.addWidget(self.dispBal, 4, 1)
      layoutDetails.setColumnStretch(0,0)
      layoutDetails.setColumnStretch(1,1)
      frmDetails = QtWidgets.QFrame()
      frmDetails.setLayout(layoutDetails)
      frmDetails.setFrameStyle(STYLE_SUNKEN)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblSpendFromLB)
      layout.addWidget(frmDetails)

      self.setLayout(layout)

# This class has all of the select wallet display and control functionality for
# selecting a wallet, and doing coin control. It can be dropped into any dialog
# and will interface with the dialog with select wlt and coin control callbacks.
class SelectWalletFrame(ArmoryFrame):
   def __init__(self, parent, main, layoutDir=VERTICAL,
      firstSelect=None,
      onlyMyWallets=False,
      wltIDList=None,
      atLeast=0,
      selectWltCallback=None,
      coinControlCallback=None,
      onlyOfflineWallets=False,
      RBFcallback=None):

      super(SelectWalletFrame, self).__init__(parent, main)
      self.coinControlCallback = coinControlCallback
      self.RBFcallback = RBFcallback

      self.dlgcc = None
      self.dlgrbf = None

      self.walletComboBox = QtWidgets.QComboBox()
      self.walletListBox  = QtWidgets.QListWidget()
      self.balAtLeast = atLeast
      self.selectWltCallback = selectWltCallback
      self.doVerticalLayout = layoutDir==VERTICAL

      if self.main and len(self.main.walletMap) == 0:
         QtWidgets.QMessageBox.critical(self, self.tr('No Wallets!'), \
            self.tr('There are no wallets to select from. Please create or import '
            'a wallet first.'), QtWidgets.QMessageBox.Ok)
         self.accept()
         return

      self.wltIDList = wltIDList if wltIDList else self.getWalletIdList(onlyOfflineWallets)

      selectedWltIndex = 0
      self.selectedID = None
      wltItems = 0
      self.displayIDs = []
      if len(self.wltIDList) > 0:
         self.selectedID = self.wltIDList[0]
         for wltID in self.wltIDList:
            wlt = self.main.walletMap[wltID]
            wlttype = determineWalletType(wlt, self.main)[0]
            if onlyMyWallets and wlttype == WLTTYPES.WatchOnly:
               continue

            self.displayIDs.append(wltID)
            if self.doVerticalLayout:
               self.walletComboBox.addItem(wlt.getDisplayStr())
            else:
               self.walletListBox.addItem(QListWidgetItem(wlt.getDisplayStr()))

            if wltID == firstSelect:
               selectedWltIndex = wltItems
               self.selectedID = wltID
            wltItems += 1
            
         if self.doVerticalLayout:
            self.walletComboBox.setCurrentIndex(selectedWltIndex)
         else:
            self.walletListBox.setCurrentRow(selectedWltIndex)


      self.walletComboBox.currentIndexChanged.connect(self.updateOnWalletChange)
      self.walletListBox.currentRowChanged.connect(self.updateOnWalletChange)

      # Start the layout
      layout =  QtWidgets.QVBoxLayout()

      lbls = []
      lbls.append(QRichLabel(self.tr("Wallet ID:"), doWrap=False))
      lbls.append(QRichLabel(self.tr("Name:"), doWrap=False))
      lbls.append(QRichLabel(self.tr("Description:"), doWrap=False))
      lbls.append(QRichLabel(self.tr("Spendable BTC:"), doWrap=False))

      for i in range(len(lbls)):
         lbls[i].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)
         lbls[i].setText('<b>' + str(lbls[i].text()) + '</b>')

      self.dispID = QRichLabel('')
      self.dispName = QRichLabel('')
      self.dispName.setWordWrap(True)
      # This line fixes squished text when word wrapping
      self.dispName.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Preferred)
      self.dispDescr = QRichLabel('')
      self.dispDescr.setWordWrap(True)
      # This line fixes squished text when word wrapping
      self.dispDescr.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Preferred)
      self.dispBal = QMoneyLabel(0)
      self.dispBal.setTextFormat(QtCore.Qt.RichText)
      
      wltInfoFrame = QtWidgets.QFrame()
      wltInfoFrame.setFrameStyle(STYLE_SUNKEN)
      wltInfoFrame.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Preferred)
      frmLayout = QtWidgets.QGridLayout()
      for i in range(len(lbls)):
         frmLayout.addWidget(lbls[i], i, 0, 1, 1)

      self.dispID.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)
      self.dispName.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)
      self.dispDescr.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)
      self.dispBal.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)
      self.dispDescr.setMinimumWidth(tightSizeNChar(self.dispDescr, 30)[0])
      frmLayout.addWidget(self.dispID,     0, 2, 1, 1)
      frmLayout.addWidget(self.dispName,   1, 2, 1, 1)
      frmLayout.addWidget(self.dispDescr,  2, 2, 1, 1)
      frmLayout.addWidget(self.dispBal,    3, 2, 1, 1)

      if coinControlCallback:
         self.lblCoinCtrl = QRichLabel(self.tr('Source: All addresses'), doWrap=False)
         frmLayout.addWidget(self.lblCoinCtrl, 4, 2, 1, 1)

         self.lblRBF = QRichLabel(self.tr('Source: N/A'))
         frmLayout.addWidget(self.lblRBF, 5, 2, 1, 1)

         self.btnCoinCtrl = QtWidgets.QPushButton(self.tr('Coin Control'))
         self.btnCoinCtrl.clicked.connect(self.doCoinCtrl)

         self.btnRBF = QtWidgets.QPushButton(self.tr('RBF Control'))
         self.btnRBF.clicked.connect(self.doRBF)

         frmLayout.addWidget(self.btnCoinCtrl, 4, 0, 1, 2)
         frmLayout.addWidget(self.btnRBF, 5, 0, 1, 2)

      frmLayout.setColumnStretch(0, 1)
      frmLayout.setColumnStretch(1, 1)
      frmLayout.setColumnStretch(2, 1)
      frmLayout.addItem(QtWidgets.QSpacerItem(20, 10, QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding), 0, 1, 4, 1)
      wltInfoFrame.setLayout(frmLayout)

      if self.doVerticalLayout:
         layout.addWidget(makeLayoutFrame(VERTICAL, [self.walletComboBox, wltInfoFrame]) )
      else:
         layout.addWidget(makeLayoutFrame(HORIZONTAL, [self.walletListBox, wltInfoFrame]) )

      self.setLayout(layout)

      # Make sure this is called once so that the default selection is displayed
      self.updateOnWalletChange()


   def getWalletIdList(self, onlyOfflineWallets):
      result = []
      if onlyOfflineWallets:
         result = self.main.getWatchingOnlyWallets()
      else:
         result = list(self.main.walletIDList)
      return result


   def getSelectedWltID(self):
      idx = -1
      if self.doVerticalLayout:
         idx = self.walletComboBox.currentIndex()
      else:
         idx = self.walletListBox.currentRow()

      return '' if idx<0 else self.displayIDs[idx]

   def doCoinCtrl(self):
      wlt = self.main.walletMap[self.getSelectedWltID()]
      if self.dlgcc == None:
         self.dlgcc = \
            CoinControlDlg(self, self.main, wlt)

      if not self.dlgcc.exec_():
         return

      self.customUtxoList = self.dlgcc.getCustomUtxoList()
      self.altBalance = sum([x.getValue() for x in self.customUtxoList])
      self.useAllCCList = self.dlgcc.isUseAllChecked()
      
      #reset RBF label to signify RBF and by coin control are mutually exclusive
      self.lblRBF.setText(self.tr("Source: N/A"))

      #update coin control label
      nUtxo = len(self.customUtxoList)
      if self.altBalance == wlt.getBalance('Spendable'):
         self.lblCoinCtrl.setText(self.tr('Source: All addresses'))
      elif nUtxo == 0:
         self.lblCoinCtrl.setText(self.tr('Source: None selected'))
      elif nUtxo == 1:
         utxo = self.customUtxoList[0]
         binAddr = utxo.getRecipientScrAddr()
         aStr = TheBridge.scriptUtils.getAddrStrForScrAddr(binAddr)
         self.lblCoinCtrl.setText(self.tr('Source: %s...' % aStr[:12]))
      elif nUtxo > 1:
         self.lblCoinCtrl.setText(self.tr('Source: %d Outputs' % nUtxo))

      self.updateOnCoinControl()

   def doRBF(self):
      wlt = self.main.walletMap[self.getSelectedWltID()]
      if self.dlgrbf == None:
         self.dlgrbf = \
            RBFDlg(self, self.main, wlt)

      if not self.dlgrbf.exec_():
         return

      self.customUtxoList = self.dlgrbf.getRBFUtxoList()
      self.altBalance = sum([x.getValue() for x in self.customUtxoList])

      nUtxo = len(self.customUtxoList)

      #no outputs selected is treated as a cancellation
      if nUtxo == 0:
         return

      self.updateRBFLabel()
      self.updateOnRBF()

   def updateRBFLabel(self):
      #reset coin control label to signify RBF and coin control are mutually exclusive
      self.lblCoinCtrl.setText(self.tr('Source: N/A'))

      nUtxo = len(self.customUtxoList)
      if nUtxo == 1:
         utxo = self.customUtxoList[0]
         aStr = TheBridge.scriptUtils.getAddrStrForScrAddr(utxo.getRecipientScrAddr())
         self.lblRBF.setText(self.tr('Source: %s...' % aStr[:12]))
      else:
         self.lblRBF.setText(self.tr("Source: %s Outputs" % str(nUtxo)))

   def updateOnWalletChange(self, ignoredInt=None):
      """
      "ignoredInt" is because the signals should call this function with the
      selected index of the relevant container, but we grab it again anyway
      using getSelectedWltID()
      """

      wltID = self.getSelectedWltID()

      if len(wltID) > 0:
         wlt = self.main.walletMap[wltID]

         self.dispID.setText(wltID)
         self.dispName.setText(wlt.labelName)
         self.dispDescr.setText(wlt.labelDescr)
         self.selectedID = wltID

         if not TheBDM.getState() == BDM_BLOCKCHAIN_READY:
            self.dispBal.setText('-' * 12)
         else:
            bal = wlt.getBalance('Spendable')
            balStr = coin2str(wlt.getBalance('Spendable'), maxZeros=1)
            if bal <= self.balAtLeast:
               self.dispBal.setText('<font color="red"><b>%s</b></font>' % balStr)
            else:
               self.dispBal.setText('<b>' + balStr + '</b>')     

         if self.selectWltCallback:
            self.selectWltCallback(wlt)

         self.repaint()
         # Reset the coin control variables after a new wallet is selected
         if self.coinControlCallback:
            self.altBalance = None
            self.customUtxoList = None
            self.useAllCCList = False
            self.btnCoinCtrl.setEnabled(wlt.getBalance('Spendable')>0)
            self.lblCoinCtrl.setText(self.tr('Source: All addresses') if wlt.getBalance('Spendable')>0 else\
                                     self.tr('Source: 0 addresses' ))
            self.dlgcc = None
            self.dlgrbf = None
            self.updateOnCoinControl()

   def updateOnCoinControl(self):
      wlt = self.main.walletMap[self.getSelectedWltID()]
      fullBal = wlt.getBalance('Spendable')
      useAllAddr = (self.altBalance == fullBal or self.altBalance == None)

      if useAllAddr:
         self.dispID.setText(wlt.uniqueIDB58)
         self.dispName.setText(wlt.labelName)
         self.dispDescr.setText(wlt.labelDescr)
         if fullBal == 0:
            self.dispBal.setText('0.0', color='TextYellow', bold=True)
         else:
            self.dispBal.setValueText(fullBal, wBold=True)
      else:
         self.dispID.setText(wlt.uniqueIDB58 + '*')
         self.dispName.setText(wlt.labelName + '*')
         self.dispDescr.setText(self.tr('*Coin Control Subset*'), color='TextBlue', bold=True)
         self.dispBal.setText(coin2str(self.altBalance, maxZeros=0), color='TextBlue')
         rawValTxt = str(self.dispBal.text())
         self.dispBal.setText(rawValTxt + ' <font color="%s">(of %s)</font>' % \
                                    (htmlColor('DisableFG'), coin2str(fullBal, maxZeros=0)))

      if not TheBDM.getState() == BDM_BLOCKCHAIN_READY:
         self.dispBal.setText(self.tr('(available when online)'), color='DisableFG')

      if self.coinControlCallback:
         self.coinControlCallback(\
            self.customUtxoList, self.altBalance, self.useAllCCList)

   def updateOnRBF(self, verbose=False):
      self.dispDescr.setText(self.tr('*RBF subset*'), color='optInRBF', bold=True)
      self.dispBal.setText(coin2str(self.altBalance, maxZeros=0), color='TextRed')

      self.updateRBFLabel()

      if not TheBDM.getState() == BDM_BLOCKCHAIN_READY:
         self.dispBal.setText(self.tr('(available when online)'), color='DisableFG')

      self.repaint()
      if self.RBFcallback:
         self.RBFcallback(self.customUtxoList, self.altBalance, verbose)

# Container for controls used in configuring a wallet to be added to any
# dialog or wizard. Currently it is only used the create wallet wizard.
# Just has Name and Description
# Advanced options have just been moved to their own frame to be used in 
# the restore wallet dialog as well.
class NewWalletFrame(ArmoryFrame):

   def __init__(self, parent, main, initLabel=''):
      super(NewWalletFrame, self).__init__(parent, main)
      self.editName = QtWidgets.QLineEdit()
      self.editName.setMinimumWidth(tightSizeNChar(self.editName,\
                                 WALLET_DATA_ENTRY_FIELD_WIDTH)[0])
      self.editName.setText(initLabel)
      lblName = QtWidgets.QLabel(self.tr("Wallet &name:"))
      lblName.setBuddy(self.editName)

      self.editDescription = QtWidgets.QTextEdit()
      self.editDescription.setMaximumHeight(75)
      self.editDescription.setMinimumWidth(tightSizeNChar(self.editDescription,\
                                 WALLET_DATA_ENTRY_FIELD_WIDTH)[0])
      lblDescription = QtWidgets.QLabel(self.tr("Wallet &description:"))
      lblDescription.setAlignment(QtCore.Qt.AlignVCenter)
      lblDescription.setBuddy(self.editDescription)

      self.useManualEntropy = QtWidgets.QCheckBox()
      lblManualEntropy = QtWidgets.QLabel(self.tr("Add Manual &Entropy"))
      lblManualEntropy.setAlignment(QtCore.Qt.AlignVCenter)
      lblManualEntropy.setBuddy(self.useManualEntropy)

   
      # breaking this up into tabs
      frameLayout = QtWidgets.QVBoxLayout()
      newWalletTabs = QtWidgets.QTabWidget()
      
      #### Basic Tab
      nameFrame = makeHorizFrame([lblName, STRETCH, self.editName])
      descriptionFrame = makeHorizFrame([lblDescription,
                                         STRETCH, self.editDescription])
      entropyFrame = makeHorizFrame([self.useManualEntropy,
                                     lblManualEntropy, STRETCH])
      basicQTab = makeVertFrame([nameFrame, descriptionFrame,
                                 entropyFrame, STRETCH])
      newWalletTabs.addTab(basicQTab, self.tr("Configure"))
      
      # Fork watching-only wallet
      self.advancedOptionsTab = AdvancedOptionsFrame(parent, main)
      newWalletTabs.addTab(self.advancedOptionsTab, self.tr("Advanced Options"))

      frameLayout.addWidget(newWalletTabs)
      self.setLayout(frameLayout)

      # These help us collect entropy as the user goes through the wizard
      # to be used for wallet creation
      self.main.registerWidgetActivateTime(self)

      
   def getKdfSec(self):
      return self.advancedOptionsTab.getKdfSec()

   def getKdfBytes(self):
      return self.advancedOptionsTab.getKdfBytes()

   def getManualEncryption(self):
      return self.useManualEntropy.isChecked()
   
   def getName(self):
      return str(self.editName.text())

   def getDescription(self):
      return str(self.editDescription.toPlainText())

class CardDeckFrame(ArmoryFrame):
   def __init__(self, parent, main, initLabel=''):
      super(CardDeckFrame, self).__init__(parent, main)

      layout = QtWidgets.QGridLayout()
      
      lblDlgDescr = QtWidgets.QLabel(self.tr('Please shuffle a deck of cards and enter the first 40 cards in order below to get at least 192 bits of entropy to properly randomize.\n\n'))
      lblDlgDescr.setWordWrap(True)
      layout.addWidget(lblDlgDescr, 0, 0, 1, 13)

      self.cards = []

      for row, suit in enumerate('shdc'):
         for col, rank in enumerate('A23456789TJQK'):
            card = QPixMapButton(':%s%s.png' %(rank,suit))
            card.nameText = rank + suit
            card.clicked.connect(self.cardClicked)

            layout.addWidget(card,row+1, col, 1, 1)
            self.cards.append(card)

      self.currentDeck = QtWidgets.QLabel("")
      layout.addWidget(self.currentDeck, 5,0,1,13)
      self.currentNum = QtWidgets.QLabel("")
      layout.addWidget(self.currentNum, 6,0,1,13)

      self.cardCount = 0

      self.setLayout(layout)

   def cardClicked(self):
      # we need to know which one was clicked
      button = self.sender()
      card = button.nameText
      self.currentDeck.setText(self.currentDeck.text() + " " + card)

      self.cardCount += 1
      self.cards.append(card)
      button.setDisabled(True)
      bits = int(math.log(
         math.factorial(52) / math.factorial(52-self.cardCount),2))
      self.currentNum.setText(self.tr("Entropy: %d bits" % bits))

   def getEntropy(self):
      cards = filter(lambda x: x != '', str(self.currentDeck.text()).split(' '))
      
      orderedCards = []
      for suit in 'shdc':
         for rank in 'A23456789TJQK':
            orderedCards.append(rank+suit)

      num = 0
      for card in cards:
         num *= len(orderedCards)
         curIndex = orderedCards.index(card)
         orderedCards = orderedCards[:curIndex] + orderedCards[curIndex+1:]
         num += curIndex
      return num

   def hasGoodEntropy(self):
      # 52!/13! > 2**192
      return self.cardCount >= 39

      
class SetPassphraseFrame(ArmoryFrame):
   def __init__(self, parent, main, initLabel='', passphraseCallback=None):
      super(SetPassphraseFrame, self).__init__(parent, main)
      self.passphraseCallback = passphraseCallback
      layout = QtWidgets.QGridLayout()
      lblDlgDescr = QtWidgets.QLabel(self.tr('Please enter a passphrase for wallet encryption.\n\n'
                           'A good passphrase consists of at least 10 or more\n'
                           'random letters, or 6 or more random words.\n'))
      lblDlgDescr.setWordWrap(True)
      layout.addWidget(lblDlgDescr, 0, 0, 1, 2)
      lblPwd1 = QtWidgets.QLabel(self.tr("New Passphrase:"))
      self.editPasswd1 = QtWidgets.QLineEdit()
      self.editPasswd1.setEchoMode(QtWidgets.QLineEdit.Password)
      self.editPasswd1.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      lblPwd2 = QtWidgets.QLabel(self.tr("Again:"))
      self.editPasswd2 = QtWidgets.QLineEdit()
      self.editPasswd2.setEchoMode(QtWidgets.QLineEdit.Password)
      self.editPasswd2.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      layout.addWidget(lblPwd1, 1, 0)
      layout.addWidget(lblPwd2, 2, 0)
      layout.addWidget(self.editPasswd1, 1, 1)
      layout.addWidget(self.editPasswd2, 2, 1)

      self.lblMatches = QtWidgets.QLabel(' ' * 20)
      self.lblMatches.setTextFormat(QtCore.Qt.RichText)
      layout.addWidget(self.lblMatches, 3, 1)
      self.setLayout(layout)
      self.editPasswd1.textChanged.connect(self.checkPassphrase)
      self.editPasswd2.textChanged.connect(self.checkPassphrase)


      # These help us collect entropy as the user goes through the wizard
      # to be used for wallet creation
      self.main.registerWidgetActivateTime(self)


   # This function is multi purpose. It updates the screen and validates the passphrase
   def checkPassphrase(self, sideEffects=True):
      result = True
      p1 = self.editPasswd1.text()
      p2 = self.editPasswd2.text()
      goodColor = htmlColor('TextGreen')
      badColor = htmlColor('TextRed')
      if not isASCII(str(p1)) or \
         not isASCII(str(p2)):
         if sideEffects:
            self.lblMatches.setText(self.tr('<font color=%s><b>Passphrase is non-ASCII!</b></font>' % badColor))
         result = False
      elif not p1 == p2:
         if sideEffects:
            self.lblMatches.setText(self.tr('<font color=%s><b>Passphrases do not match!</b></font>' % badColor))
         result = False
      elif len(p1) < 5:
         if sideEffects:
            self.lblMatches.setText(self.tr('<font color=%s><b>Passphrase is too short!</b></font>' % badColor))
         result = False
      if sideEffects:
         if result:
            self.lblMatches.setText(self.tr('<font color=%s><b>Passphrases match!</b></font>' % goodColor))
         if self.passphraseCallback:
            self.passphraseCallback()
      return result

   def getPassphrase(self):
      return str(self.editPasswd1.text())
   
class VerifyPassphraseFrame(ArmoryFrame):
   def __init__(self, parent, main, initLabel=''):
      super(VerifyPassphraseFrame, self).__init__(parent, main)
      lblWarnImgL = QtWidgets.QLabel()
      lblWarnImgL.setPixmap(QtGui.QPixmap(':/MsgBox_warning48.png'))
      lblWarnImgL.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      lblWarnTxt1 = QRichLabel(\
         self.tr('<font color="red"><b>!!! DO NOT FORGET YOUR PASSPHRASE !!!</b></font>'), size=4)
      lblWarnTxt1.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      lblWarnTxt2 = QRichLabel(\
         self.tr('<b>No one can help you recover you bitcoins if you forget the '
         'passphrase and don\'t have a paper backup!</b> Your wallet and '
         'any <u>digital</u> backups are useless if you forget it.  '
         '<br><br>'
         'A <u>paper</u> backup protects your wallet forever, against '
         'hard-drive loss and losing your passphrase.  It also protects you '
         'from theft, if the wallet was encrypted and the paper backup '
         'was not stolen with it.  Please make a paper backup and keep it in '
         'a safe place.'
         '<br><br>'
         'Please enter your passphrase a third time to indicate that you '
         'are aware of the risks of losing your passphrase!</b>'), doWrap=True)


      self.edtPasswd3 = QtWidgets.QLineEdit()
      self.edtPasswd3.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd3.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblWarnImgL, 0, 0, 4, 1)
      layout.addWidget(lblWarnTxt1, 0, 1, 1, 1)
      layout.addWidget(lblWarnTxt2, 2, 1, 1, 1)
      layout.addWidget(self.edtPasswd3, 5, 1, 1, 1)
      self.setLayout(layout)

      # These help us collect entropy as the user goes through the wizard
      # to be used for wallet creation
      self.main.registerWidgetActivateTime(self)

      
class WalletBackupFrame(ArmoryFrame):
   # Some static enums, and a QtWidgets.QRadioButton with mouse-enter/mouse-leave events
   FEATURES = enum('ProtGen', 'ProtImport', 'LostPass', 'Durable', \
                   'Visual', 'Physical', 'Count')
   OPTIONS = enum('Paper1', 'PaperN', 'DigPlain', 'DigCrypt', 'Export', 'Count')
   def __init__(self, parent, main, initLabel=''):
      super(WalletBackupFrame, self).__init__(parent, main)
      # Don't have a wallet yet so assume false.
      self.hasImportedAddr = False
      self.isBackupCreated = False
      self.passphrase = None
      self.lblTitle = QRichLabel(self.tr("<b>Backup Options</b>"))
      lblTitleDescr = QRichLabel(self.tr(
         'Armory wallets only need to be backed up <u>one time, ever.</u> '
         'The backup is good no matter how many addresses you use. '))
      lblTitleDescr.setOpenExternalLinks(True)


      self.optPaperBackupTop = QRadioButtonBackupCtr(self, \
                                    self.tr('Printable Paper Backup'), self.OPTIONS.Paper1)
      self.optPaperBackupOne = QRadioButtonBackupCtr(self, \
                                    self.tr('Single-Sheet (Recommended)'), self.OPTIONS.Paper1)
      self.optPaperBackupFrag = QRadioButtonBackupCtr(self, \
                                    self.tr('Fragmented Backup (M-of-N)'), self.OPTIONS.PaperN)

      self.optDigitalBackupTop = QRadioButtonBackupCtr(self, \
                                    self.tr('Digital Backup'), self.OPTIONS.DigPlain)
      self.optDigitalBackupPlain = QRadioButtonBackupCtr(self, \
                                    self.tr('Unencrypted'), self.OPTIONS.DigPlain)
      self.optDigitalBackupCrypt = QRadioButtonBackupCtr(self, \
                                    self.tr('Encrypted'), self.OPTIONS.DigCrypt)

      self.optIndivKeyListTop = QRadioButtonBackupCtr(self, \
                                    self.tr('Export Key Lists'), self.OPTIONS.Export)


      self.optPaperBackupTop.setFont(GETFONT('Var', bold=True))
      self.optDigitalBackupTop.setFont(GETFONT('Var', bold=True))
      self.optIndivKeyListTop.setFont(GETFONT('Var', bold=True))

      # I need to be able to unset the sub-options when they become disabled
      self.optPaperBackupNONE = QtWidgets.QRadioButton('')
      self.optDigitalBackupNONE = QtWidgets.QRadioButton('')

      btngrpTop = QtWidgets.QButtonGroup(self)
      btngrpTop.addButton(self.optPaperBackupTop)
      btngrpTop.addButton(self.optDigitalBackupTop)
      btngrpTop.addButton(self.optIndivKeyListTop)
      btngrpTop.setExclusive(True)

      btngrpPaper = QtWidgets.QButtonGroup(self)
      btngrpPaper.addButton(self.optPaperBackupNONE)
      btngrpPaper.addButton(self.optPaperBackupOne)
      btngrpPaper.addButton(self.optPaperBackupFrag)
      btngrpPaper.setExclusive(True)

      btngrpDig = QtWidgets.QButtonGroup(self)
      btngrpDig.addButton(self.optDigitalBackupNONE)
      btngrpDig.addButton(self.optDigitalBackupPlain)
      btngrpDig.addButton(self.optDigitalBackupCrypt)
      btngrpDig.setExclusive(True)

      self.optPaperBackupTop.clicked.connect(self.optionClicked)
      self.optPaperBackupOne.clicked.connect(self.optionClicked)
      self.optPaperBackupFrag.clicked.connect(self.optionClicked)
      self.optDigitalBackupTop.clicked.connect(self.optionClicked)
      self.optDigitalBackupPlain.clicked.connect(self.optionClicked)
      self.optDigitalBackupCrypt.clicked.connect(self.optionClicked)
      self.optIndivKeyListTop.clicked.connect(self.optionClicked)


      spacer = lambda: QtWidgets.QSpacerItem(20, 1, QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding)
      layoutOpts = QtWidgets.QGridLayout()
      layoutOpts.addWidget(self.optPaperBackupTop, 0, 0, 1, 2)
      layoutOpts.addItem(spacer(), 1, 0)
      layoutOpts.addItem(spacer(), 2, 0)
      layoutOpts.addWidget(self.optDigitalBackupTop, 3, 0, 1, 2)
      layoutOpts.addItem(spacer(), 4, 0)
      layoutOpts.addItem(spacer(), 5, 0)
      layoutOpts.addWidget(self.optIndivKeyListTop, 6, 0, 1, 2)

      layoutOpts.addWidget(self.optPaperBackupOne, 1, 1)
      layoutOpts.addWidget(self.optPaperBackupFrag, 2, 1)
      layoutOpts.addWidget(self.optDigitalBackupPlain, 4, 1)
      layoutOpts.addWidget(self.optDigitalBackupCrypt, 5, 1)
      layoutOpts.setColumnStretch(0, 0)
      layoutOpts.setColumnStretch(1, 1)

      frmOpts = QtWidgets.QFrame()
      frmOpts.setLayout(layoutOpts)
      frmOpts.setFrameStyle(STYLE_SUNKEN)


      self.featuresTips = [None] * self.FEATURES.Count
      self.featuresLbls = [None] * self.FEATURES.Count
      self.featuresImgs = [None] * self.FEATURES.Count


      F = self.FEATURES
      self.featuresTips[F.ProtGen] = createToolTipWidget(self.tr(
         'Every time you click "Receive Bitcoins," a new address is generated. '
         'All of these addresses are generated from a single seed value, which '
         'is included in all backups.   Therefore, all addresses that you have '
         'generated so far <b>and</b> will ever be generated with this wallet, '
         'are protected by this backup! '))

      self.featuresTips[F.ProtImport] = createToolTipWidget(self.tr(
         '<i>This wallet <u>does not</u> currently have any imported '
         'addresses, so you can safely ignore this feature!</i> '
         'When imported addresses are present, backups only protects those '
         'imported before the backup was made.  You must replace that '
         'backup if you import more addresses! '))

      self.featuresTips[F.LostPass] = createToolTipWidget(self.tr(
         'Lost/forgotten passphrases are, <b>by far</b>, the most common '
         'reason for users losing bitcoins.  It is critical you have '
         'at least one backup that works if you forget your wallet '
         'passphrase. '))

      self.featuresTips[F.Durable] = createToolTipWidget(self.tr(
         'USB drives and CD/DVD disks are not intended for long-term storage. '
         'They will <i>probably</i> last many years, but not guaranteed '
         'even for 3-5 years.   On the other hand, printed text on paper will '
         'last many decades, and useful even when thoroughly faded. '))

      self.featuresTips[F.Visual] = createToolTipWidget(self.tr(
         'The ability to look at a backup and determine if '
         'it is still usable.   If a digital backup is stored in a safe '
         'deposit box, you have no way to verify its integrity unless '
         'you take a secure computer/device with you.  A simple glance at '
         'a paper backup is enough to verify that it is still intact. '))

      self.featuresTips[F.Physical] = createToolTipWidget(self.tr(
         'If multiple pieces/fragments are required to restore this wallet. '
         'For instance, encrypted backups require the backup '
         '<b>and</b> the passphrase.  This feature is only needed for those '
         'concerned about physical security, not just online security.'))


      MkFeatLabel = lambda x: QRichLabel(x, doWrap=False)
      self.featuresLbls[F.ProtGen] = MkFeatLabel(self.tr('Protects All Future Addresses'))
      self.featuresLbls[F.ProtImport] = MkFeatLabel(self.tr('Protects Imported Addresses'))
      self.featuresLbls[F.LostPass] = MkFeatLabel(self.tr('Forgotten Passphrase'))
      self.featuresLbls[F.Durable] = MkFeatLabel(self.tr('Long-term Durability'))
      self.featuresLbls[F.Visual] = MkFeatLabel(self.tr('Visual Integrity'))
      self.featuresLbls[F.Physical] = MkFeatLabel(self.tr('Multi-Point Protection'))

      if not self.hasImportedAddr:
         self.featuresLbls[F.ProtImport].setEnabled(False)

      self.lblSelFeat = QRichLabel('', doWrap=False, hAlign=QtCore.Qt.AlignHCenter)

      layoutFeat = QtWidgets.QGridLayout()
      layoutFeat.addWidget(self.lblSelFeat, 0, 0, 1, 3)
      layoutFeat.addWidget(HLINE(), 1, 0, 1, 3)
      for i in range(self.FEATURES.Count):
         self.featuresImgs[i] = QtWidgets.QLabel('')
         layoutFeat.addWidget(self.featuresTips[i], i + 2, 0)
         layoutFeat.addWidget(self.featuresLbls[i], i + 2, 1)
         layoutFeat.addWidget(self.featuresImgs[i], i + 2, 2)
      layoutFeat.setColumnStretch(0, 0)
      layoutFeat.setColumnStretch(1, 1)
      layoutFeat.setColumnStretch(2, 0)

      frmFeat = QtWidgets.QFrame()
      frmFeat.setLayout(layoutFeat)
      frmFeat.setFrameStyle(STYLE_SUNKEN)


      self.lblDescrSelected = QRichLabel('')
      frmFeatDescr = makeVertFrame([self.lblDescrSelected])
      self.lblDescrSelected.setMinimumHeight(tightSizeNChar(self, 10)[1] * 8)

      self.btnDoIt = QtWidgets.QPushButton(self.tr('Create Backup'))
      self.btnDoIt.clicked.connect(self.clickedDoIt)

      layout = QtWidgets.QGridLayout()
      layout.addWidget(self.lblTitle, 0, 0, 1, 2)
      layout.addWidget(lblTitleDescr, 1, 0, 1, 2)
      layout.addWidget(frmOpts, 2, 0)
      layout.addWidget(frmFeat, 2, 1)
      layout.addWidget(frmFeatDescr, 3, 0, 1, 2)
      layout.addWidget(self.btnDoIt, 4, 0, 1, 2)
      layout.setRowStretch(0, 0)
      layout.setRowStretch(1, 0)
      layout.setRowStretch(2, 0)
      layout.setRowStretch(3, 1)
      layout.setRowStretch(4, 0)
      self.setLayout(layout)
      self.setMinimumSize(640, 350)

      self.optPaperBackupTop.setChecked(True)
      self.optPaperBackupOne.setChecked(True)
      self.setDispFrame(-1)
      self.optionClicked()
      
   #############################################################################
   def setWallet(self, wlt):
      self.wlt = wlt
      wltID = wlt.uniqueIDB58
      wltName = wlt.labelName
      self.hasImportedAddr = self.wlt.hasAnyImported()
      # Highlight imported-addr feature if their wallet contains them
      pcolor = 'TextWarn' if self.hasImportedAddr else 'DisableFG'
      self.featuresLbls[self.FEATURES.ProtImport].setText(self.tr(\
         'Protects Imported Addresses'), color=pcolor)

      if self.hasImportedAddr:
         self.featuresTips[self.FEATURES.ProtImport].setText(self.tr(
            'When imported addresses are present, backups only protects those '
            'imported before the backup was made!  You must replace that '
            'backup if you import more addresses! '
            '<i>Your wallet <u>does</u> contain imported addresses</i>.'))

      self.lblTitle.setText(self.tr('<b>Backup Options for Wallet "%s" (%s)</b>' % (wltName, wltID)))

   #############################################################################
   def setDispFrame(self, index):
      if index < 0:
         self.setDispFrame(self.getIndexChecked())
      else:
         # Highlight imported-addr feature if their wallet contains them
         pcolor = 'TextWarn' if self.hasImportedAddr else 'DisableFG'
         self.featuresLbls[self.FEATURES.ProtImport].setText(self.tr(\
            'Protects Imported Addresses'), color=pcolor)

         txtPaper = self.tr(
               'Paper backups protect every address ever generated by your '
               'wallet. It is unencrypted, which means it needs to be stored '
               'in a secure place, but it will help you recover your wallet '
               'if you forget your encryption passphrase! '
               '<br><br>'
               '<b>You don\'t need a printer to make a paper backup! '
               'The data can be copied by hand with pen and paper.</b> '
               'Paper backups are preferred to digital backups, because you '
               'know the paper backup will work no matter how many years (or '
               'decades) it sits in storage.')
         txtDigital = self.tr(
               'Digital backups can be saved to an external hard-drive or '
               'USB removable media.  It is recommended you make a few '
               'copies to protect against "bit rot" (degradation). <br><br>')
         txtDigPlain = self.tr(
               '<b><u>IMPORTANT:</u> Do not save an unencrypted digital '
               'backup to your primary hard drive!</b> '
               'Please save it <i>directly</i> to the backup device. '
               'Deleting the file does not guarantee the data is actually '
               'gone!')
         txtDigCrypt = self.tr(
               '<b><u>IMPORTANT:</u> It is critical that you have at least '
               'one unencrypted backup!</b>  Without it, your bitcoins will '
               'be lost forever if you forget your passphrase!  This is <b> '
               'by far</b> the most common reason users lose coins!  Having '
               'at least one paper backup is recommended.')
         txtIndivKeys = self.tr(
               'View and export invidivual addresses strings, '
               'public keys and/or private keys contained in your wallet. '
               'This is useful for exporting your private keys to be imported into '
               'another wallet app or service. '
               '<br><br>'
               'You can view/backup imported keys, as well as unused keys in your '
               'keypool (pregenerated addresses protected by your backup that '
               'have not yet been used).')


         chk = lambda: QtGui.QPixmap(':/checkmark32.png').scaled(20, 20)
         _X_ = lambda: QtGui.QPixmap(':/red_X.png').scaled(16, 16)
         if index == self.OPTIONS.Paper1:
            self.lblSelFeat.setText(self.tr('Single-Sheet Paper Backup'), bold=True)
            self.featuresImgs[self.FEATURES.ProtGen   ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.ProtImport].setPixmap(chk())
            self.featuresImgs[self.FEATURES.LostPass  ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Durable   ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Visual    ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Physical  ].setPixmap(_X_())
            self.lblDescrSelected.setText(txtPaper)
         elif index == self.OPTIONS.PaperN:
            self.lblSelFeat.setText(self.tr('Fragmented Paper Backup'), bold=True)
            self.featuresImgs[self.FEATURES.ProtGen   ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.ProtImport].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.LostPass  ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Durable   ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Visual    ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Physical  ].setPixmap(chk())
            self.lblDescrSelected.setText(txtPaper)
         elif index == self.OPTIONS.DigPlain:
            self.lblSelFeat.setText(self.tr('Unencrypted Digital Backup'), bold=True)
            self.featuresImgs[self.FEATURES.ProtGen   ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.ProtImport].setPixmap(chk())
            self.featuresImgs[self.FEATURES.LostPass  ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Durable   ].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.Visual    ].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.Physical  ].setPixmap(_X_())
            self.lblDescrSelected.setText(txtDigital + txtDigPlain)
         elif index == self.OPTIONS.DigCrypt:
            self.lblSelFeat.setText(self.tr('Encrypted Digital Backup'), bold=True)
            self.featuresImgs[self.FEATURES.ProtGen   ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.ProtImport].setPixmap(chk())
            self.featuresImgs[self.FEATURES.LostPass  ].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.Durable   ].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.Visual    ].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.Physical  ].setPixmap(chk())
            self.lblDescrSelected.setText(txtDigital + txtDigCrypt)
         elif index == self.OPTIONS.Export:
            self.lblSelFeat.setText(self.tr('Export Key Lists'), bold=True)
            self.featuresImgs[self.FEATURES.ProtGen   ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.ProtImport].setPixmap(chk())
            self.featuresImgs[self.FEATURES.LostPass  ].setPixmap(chk())
            self.featuresImgs[self.FEATURES.Durable   ].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.Visual    ].setPixmap(_X_())
            self.featuresImgs[self.FEATURES.Physical  ].setPixmap(_X_())
            self.lblDescrSelected.setText(txtIndivKeys)
         else:
            LOGERROR('What index was sent to setDispFrame? %d', index)
            
   #############################################################################
   def getIndexChecked(self):
      if self.optPaperBackupOne.isChecked():
         return self.OPTIONS.Paper1
      elif self.optPaperBackupFrag.isChecked():
         return self.OPTIONS.PaperN
      elif self.optPaperBackupTop.isChecked():
         return self.OPTIONS.Paper1
      elif self.optDigitalBackupPlain.isChecked():
         return self.OPTIONS.DigPlain
      elif self.optDigitalBackupCrypt.isChecked():
         return self.OPTIONS.DigCrypt
      elif self.optDigitalBackupTop.isChecked():
         return self.OPTIONS.DigPlain
      elif self.optIndivKeyListTop.isChecked():
         return self.OPTIONS.Export
      else:
         return 0

   #############################################################################
   def optionClicked(self):
      if self.optPaperBackupTop.isChecked():
         self.optPaperBackupOne.setEnabled(True)
         self.optPaperBackupFrag.setEnabled(True)
         self.optDigitalBackupPlain.setEnabled(False)
         self.optDigitalBackupCrypt.setEnabled(False)
         self.optDigitalBackupPlain.setChecked(False)
         self.optDigitalBackupCrypt.setChecked(False)
         self.optDigitalBackupNONE.setChecked(True)
         self.btnDoIt.setText(self.tr('Create Paper Backup'))
      elif self.optDigitalBackupTop.isChecked():
         self.optDigitalBackupPlain.setEnabled(True)
         self.optDigitalBackupCrypt.setEnabled(True)
         self.optPaperBackupOne.setEnabled(False)
         self.optPaperBackupFrag.setEnabled(False)
         self.optPaperBackupOne.setChecked(False)
         self.optPaperBackupFrag.setChecked(False)
         self.optPaperBackupNONE.setChecked(True)
         self.btnDoIt.setText(self.tr('Create Digital Backup'))
      elif self.optIndivKeyListTop.isChecked():
         self.optPaperBackupOne.setEnabled(False)
         self.optPaperBackupFrag.setEnabled(False)
         self.optPaperBackupOne.setChecked(False)
         self.optPaperBackupFrag.setChecked(False)
         self.optDigitalBackupPlain.setEnabled(False)
         self.optDigitalBackupCrypt.setEnabled(False)
         self.optDigitalBackupPlain.setChecked(False)
         self.optDigitalBackupCrypt.setChecked(False)
         self.optDigitalBackupNONE.setChecked(True)
         self.optPaperBackupNONE.setChecked(True)
         self.btnDoIt.setText(self.tr('Export Key Lists'))
      self.setDispFrame(-1)

   def setPassphrase(self, passphrase):
      self.passphrase = passphrase
      
   def clickedDoIt(self):
      isBackupCreated = False
      
      if self.passphrase:
         from qtdialogs import DlgProgress
         unlockProgress = DlgProgress(self, self.main, HBar=1,
                                      Title=self.tr("Unlocking Wallet"))
         unlockProgress.exec_(self.wlt.unlock, 
                              securePassphrase=SecureBinaryData( \
                              self.passphrase),
                              Progress=unlockProgress.UpdateHBar)
         
      if self.optPaperBackupOne.isChecked():
         isBackupCreated = OpenPaperBackupDialog('Single', self.parent(), self.main, self.wlt)
      elif self.optPaperBackupFrag.isChecked():
         isBackupCreated = OpenPaperBackupDialog('Frag', self.parent(), self.main, self.wlt)
      elif self.optDigitalBackupPlain.isChecked():
         if self.main.digitalBackupWarning():
            isBackupCreated = self.main.makeWalletCopy(self, self.wlt, 'Decrypt', 'decrypt')
      elif self.optDigitalBackupCrypt.isChecked():
         isBackupCreated = self.main.makeWalletCopy(self, self.wlt, 'Encrypt', 'encrypt')
      elif self.optIndivKeyListTop.isChecked():
         if self.wlt.useEncryption and self.wlt.isLocked:
            dlg = DlgUnlockWallet(self.wlt, self, self.main, 'Unlock Private Keys')
            if not dlg.exec_():
               if self.main.usermode == USERMODE.Expert:
                  QtWidgets.QMessageBox.warning(self, self.tr('Unlock Failed'), self.tr(
                     'Wallet was not unlocked.  The public keys and addresses '
                     'will still be shown, but private keys will not be available '
                     'unless you reopen the dialog with the correct passphrase.'), \
                     QtWidgets.QMessageBox.Ok)
               else:
                  QtWidgets.QMessageBox.warning(self, self.tr('Unlock Failed'), self.tr(
                     'Wallet could not be unlocked to display individual keys.'), \
                     QtWidgets.QMessageBox.Ok)
                  if self.main.usermode == USERMODE.Standard:
                     return
         DlgShowKeyList(self.wlt, self.parent(), self.main).exec_()
         isBackupCreated = True
      if isBackupCreated:
         self.isBackupCreated = True


class WizardCreateWatchingOnlyWalletFrame(ArmoryFrame):

   def __init__(self, parent, main, initLabel='', backupCreatedCallback=None):
      super(WizardCreateWatchingOnlyWalletFrame, self).__init__(parent, main)


      summaryText = QRichLabel(self.tr(
               'Your wallet has been created and is ready to be used.  It will '
               'appear in the "<i>Available Wallets</i>" list in the main window. ' 
               'You may click "<i>Finish</i>" if you do not plan to use this '
               'wallet on any other computer.'
               '<br><br>'
               'A <b>watching-only wallet</b> behaves exactly like a a regular '
               'wallet, but does not contain any signing keys.  You can generate '
               'addresses and confirm receipt of payments, but not spend or move '
               'the funds in the wallet.  To move the funds, '
               'use the "<i>Offline Transactions</i>" button on the main '
               'window for directions (which involves bringing the transaction '
               'to this computer for a signature).  Or you can give the '
               'watching-only wallet to someone who needs to monitor the wallet '
               'but should not be able to move the money. '
               '<br><br>'
               'Click the button to save a watching-only copy of this wallet. '
               'Use the "<i>Import or Restore Wallet</i>" button in the '
               'upper-right corner'))
      lbtnForkWlt = QtWidgets.QPushButton('Create Watching-Only Copy')
      lbtnForkWlt.clicked.connect(self.forkOnlineWallet)
      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(summaryText)
      layout.addWidget(lbtnForkWlt)
      self.setLayout(layout)


   def forkOnlineWallet(self):
      currPath = self.wlt.walletPath
      pieces = os.path.splitext(currPath)
      currPath = pieces[0] + '_WatchOnly' + pieces[1]

      saveLoc = self.main.getFileSave('Save Watching-Only Copy', \
                                      defaultFilename=currPath)
      if not saveLoc.endswith('.wallet'):
         saveLoc += '.wallet'
      self.wlt.forkOnlineWallet(saveLoc, self.wlt.labelName, \
                             '(Watching-Only) ' + self.wlt.labelDescr)   
   
   def setWallet(self, wlt):
      self.wlt = wlt
