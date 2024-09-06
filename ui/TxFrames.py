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

import random

from qtdialogs.qtdefines import ArmoryFrame, tightSizeNChar, \
   GETFONT, QRichLabel, VLINE, QLabelButton, USERMODE, \
   VERTICAL, makeHorizFrame, STYLE_RAISED, makeVertFrame, \
   relaxedSizeNChar, STYLE_SUNKEN, CHANGE_ADDR_DESCR_STRING, \
   STRETCH, createToolTipWidget, MSGBOX

from qtdialogs.qtdialogs import NO_CHANGE
from qtdialogs.DlgDispTxInfo import DlgDispTxInfo
from qtdialogs.DlgConfirmSend import DlgConfirmSend
from qtdialogs.MsgBoxCustom import MsgBoxCustom

from armoryengine.BDM import TheBDM, BDM_BLOCKCHAIN_READY
from armoryengine.Transaction import UnsignedTransaction, getTxOutScriptType
from armoryengine.CoinSelection import PyUnspentTxOut
from ui.WalletFrames import SelectWalletFrame, LockboxSelectFrame
from armoryengine.MultiSigUtils import createLockboxEntryStr
from armoryengine.ArmoryUtils import MAX_COMMENT_LENGTH, getAddrByte, \
   LOGEXCEPT, LOGERROR, LOGINFO, NegativeValueError, TooMuchPrecisionError, \
   str2coin, CPP_TXOUT_STDSINGLESIG, CPP_TXOUT_P2SH, coin2str, \
   coin2str_approx, MIN_FEE_BYTE, getNameForAddrType, binary_to_hex
from armoryengine.Settings import TheSettings
from armoryengine.AddressUtils import hash160_to_addrStr, addrTypeInSet, \
   addrStr_to_hash160, script_to_addrStr, getAddressTypeForOutputType, \
   BadAddressError

from ui.FeeSelectUI import FeeSelectionDialog
from ui.QtExecuteSignal import TheSignalExecution
from qtpy import QtGui, QtCore, QtWidgets
from armorycolors import Colors
from armoryengine.CppBridge import TheBridge

CS_USE_FULL_CUSTOM_LIST = 1
CS_ADJUST_FEE           = 2
CS_SHUFFLE_ENTRIES      = 4

from armoryengine.AddressUtils import AddressEntryType_Default, \
   AddressEntryType_P2PKH, AddressEntryType_P2PK, AddressEntryType_P2WPKH, \
   AddressEntryType_Multisig, AddressEntryType_Uncompressed, \
   AddressEntryType_P2SH, AddressEntryType_P2WSH

################################################################################
class CoinSelectionInstance(object):
   def __init__(self, wallet, topHeight):
      self.wallet = wallet
      self.height = topHeight
      self.csInstance = None

   #############################################################################
   def __del__(self):
      if self.csInstance:
         self.csInstance.destroyCoinSelectionInstance()

   #############################################################################
   def setup(self):
      self.csInstance = self.wallet.initCoinSelectionInstance(self.height)

   #############################################################################
   def setRecipient(self, addr, value, id):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      self.csInstance.setCoinSelectionRecipient(addr, value, id)

   #############################################################################
   def updateRecipient(self, addr, value, id):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      self.csInstance.setCoinSelectionRecipient(addr, value, id)

   #############################################################################
   def resetRecipients(self):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      self.csInstance.reset()

   #############################################################################
   def selectUTXOs(self, fee, feePerByte, processFlags):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      self.csInstance.selectUTXOs(fee, feePerByte, processFlags)

   #############################################################################
   def processCustomUtxoList(self, utxoList, fee, feePerByte, processFlags):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      self.csInstance.processCustomUtxoList(
         utxoList, fee, feePerByte, processFlags)

   #############################################################################
   def getUtxoSelection(self):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      return self.csInstance.getUtxoSelection()

   #############################################################################
   def getFlatFee(self):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      return self.csInstance.getFlatFee()

   #############################################################################
   def getFeeByte(self):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      return self.csInstance.getFeeByte()

   #############################################################################
   def getSizeEstimate(self):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      return self.csInstance.getSizeEstimate()

   #############################################################################
   def getFeeForMaxVal(self, feePerByte):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      return self.csInstance.getFeeForMaxVal(feePerByte)

   #############################################################################
   def getFeeForMaxValUtxoVector(self, utxoList, feePerByte):
      if not self.csInstance:
         raise Exception("uninitialized coin selection instance")
      return self.csInstance.getFeeForMaxValUtxoVector(utxoList, feePerByte)


################################################################################
class SendBitcoinsFrame(ArmoryFrame):
   def __init__(self, parent, main, initLabel='',
                 wlt=None, wltIDList=None,
                 selectWltCallback = None, onlyOfflineWallets=False,
                 sendCallback = None, createUnsignedTxCallback = None,
                 spendFromLockboxID=None):
      super(SendBitcoinsFrame, self).__init__(parent, main)
      self.maxHeight = tightSizeNChar(GETFONT('var'), 1)[1] + 8
      self.customUtxoList = []
      self.altBalance = None
      self.useCustomListInFull = False
      self.wlt = wlt
      self.wltID = wlt.uniqueIDB58 if wlt else None
      self.wltIDList = wltIDList
      self.selectWltCallback = selectWltCallback
      self.sendCallback = sendCallback
      self.createUnsignedTxCallback = createUnsignedTxCallback
      self.lbox = self.main.getLockboxByID(spendFromLockboxID)
      self.onlyOfflineWallets = onlyOfflineWallets
      self.widgetTable = []
      self.isMax = False
      self.scrollRecipArea = QtWidgets.QScrollArea()

      lblRecip = QRichLabel('<b>Enter Recipients:</b>')
      lblRecip.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignBottom)

      self.shuffleEntries = True
      self.freeOfErrors = True

      def getWalletIdList(onlyOfflineWallets):
         result = []
         if onlyOfflineWallets:
            result = self.main.getWatchingOnlyWallets()
         else:
            result = list(self.main.walletIDList)
         return result

      self.wltIDList = wltIDList
      if wltIDList == None:
         self.wltIDList = getWalletIdList(onlyOfflineWallets)

      feetip = createToolTipWidget(\
            self.tr('Transaction fees go to users who contribute computing power to '
            'keep the Bitcoin network secure, and in return they get your transaction '
            'included in the blockchain faster.'))

      self.feeDialog = FeeSelectionDialog(self, self.main, \
                        self.resolveCoinSelection, self.getCoinSelectionState)
      self.feeLblButton = self.feeDialog.getLabelButton()

      def feeDlg():
         self.feeDialog.exec_()
      self.feeLblButton.linkActivated.connect(feeDlg)

      # This used to be in the later, expert-only section, but some of these
      # are actually getting referenced before being declared.  So moved them
      # up to here.
      self.chkDefaultChangeAddr = QtWidgets.QCheckBox(self.tr('Use an existing address for change'))
      self.radioFeedback = QtWidgets.QRadioButton(self.tr('Send change to first input address'))
      self.radioSpecify = QtWidgets.QRadioButton(self.tr('Specify a change address'))
      self.lblChangeAddr = QRichLabel(self.tr('Change:'))

      addrWidgets = self.main.createAddressEntryWidgets(\
         self, maxDetectLen=36, defaultWltID=self.wltID)
      self.edtChangeAddr  = addrWidgets['QLE_ADDR']
      self.btnChangeAddr  = addrWidgets['BTN_BOOK']
      self.lblAutoDetect  = addrWidgets['LBL_DETECT']
      self.getUserChangeScript = addrWidgets['CALLBACK_GETSCRIPT']

      self.chkRememberChng = QtWidgets.QCheckBox(self.tr('Remember for future transactions'))
      self.vertLine = VLINE()

      self.ttipSendChange = createToolTipWidget(\
            self.tr('Most transactions end up with oversized inputs and Armory will send '
            'the change to the next address in this wallet.  You may change this '
            'behavior by checking this box.'))
      self.ttipFeedback = createToolTipWidget(\
            self.tr('Guarantees that no new addresses will be created to receive '
            'change. This reduces anonymity, but is useful if you '
            'created this wallet solely for managing imported addresses, '
            'and want to keep all funds within existing addresses.'))
      self.ttipSpecify = createToolTipWidget(\
            self.tr('You can specify any valid Bitcoin address for the change. '
            '<b>NOTE:</b> If the address you specify is not in this wallet, '
            'Armory will not be able to distinguish the outputs when it shows '
            'up in your ledger.  The change will look like a second recipient, '
            'and the total debit to your wallet will be equal to the amount '
            'you sent to the recipient <b>plus</b> the change.'))
      self.ttipUnsigned = createToolTipWidget(\
         self.tr('Check this box to create an unsigned transaction to be signed'
         ' and/or broadcast later.'))
      self.unsignedCheckbox = QtWidgets.QCheckBox(self.tr('Create Unsigned'))

      self.RBFcheckbox = QtWidgets.QCheckBox(self.tr('enable RBF'))
      self.RBFcheckbox.setChecked(True)
      self.ttipRBF = createToolTipWidget(\
         self.tr('RBF flagged inputs allow to respend the underlying outpoint for a '
                 'higher fee as long as the original spending transaction remains '
                 'unconfirmed. <br><br>'
                 'Checking this box will RBF flag all inputs in this transaction'))

      self.btnSend = QtWidgets.QPushButton(self.tr('Send!'))
      self.btnCancel = QtWidgets.QPushButton(self.tr('Cancel'))
      self.btnCancel.clicked.connect(parent.reject)

      self.btnPreviewTx = QLabelButton("Preview Transaction")
      self.btnPreviewTx.linkActivated.connect(self.previewTx)

      # Created a standard wallet chooser frame. Pass the call back method
      # for when the user selects a wallet.
      if self.lbox is None:
         coinControlCallback = self.coinControlUpdate if self.main.usermode == USERMODE.Expert else None
         RBFcallback = self.RBFupdate if self.main.usermode == USERMODE.Expert else None
         self.frmSelectedWlt = SelectWalletFrame(parent, main,
                     VERTICAL,
                     self.wltID,
                     wltIDList=self.wltIDList,
                     selectWltCallback=self.setWallet, \
                     coinControlCallback=coinControlCallback,
                     onlyOfflineWallets=self.onlyOfflineWallets,
                     RBFcallback=RBFcallback)
      else:
         self.frmSelectedWlt = LockboxSelectFrame(\
            parent, main, VERTICAL, self.lbox.uniqueIDB58)
         self.setupCoinSelectionForLockbox(self.lbox)

      # Only the Create  Unsigned Transaction button if there is a callback for it.
      # Otherwise the containing dialog or wizard will provide the offlien tx button
      metaButtonList = [self.btnPreviewTx, STRETCH, self.RBFcheckbox, self.ttipRBF]

      buttonList = []
      if self.createUnsignedTxCallback:
         self.unsignedCheckbox.clicked.connect(self.unsignedCheckBoxUpdate)
         buttonList.append(self.unsignedCheckbox)
         buttonList.append(self.ttipUnsigned)

      buttonList.append(STRETCH)
      buttonList.append(self.btnCancel)

      # Only add the Send Button if there's a callback for it
      # Otherwise the containing dialog or wizard will provide the send button
      if self.sendCallback:
         self.btnSend.clicked.connect(self.createTxAndBroadcast)
         buttonList.append(self.btnSend)

      txFrm = makeHorizFrame(\
         [self.feeLblButton, feetip], STYLE_RAISED, condenseMargins=True)
      metaFrm = makeHorizFrame(\
         metaButtonList, STYLE_RAISED, condenseMargins=True)
      buttonFrame = makeHorizFrame(buttonList, condenseMargins=True)
      btnEnterURI = QtWidgets.QPushButton(self.tr('Manually Enter "bitcoin:" Link'))
      ttipEnterURI = createToolTipWidget(self.tr(
         'Armory does not always succeed at registering itself to handle '
         'URL links from webpages and email. '
         'Click this button to copy a "bitcoin:" link directly into Armory.'))
      btnEnterURI.clicked.connect(self.clickEnterURI)
      fromFrameList = [self.frmSelectedWlt]

      if not self.main.usermode == USERMODE.Standard:
         frmEnterURI = makeHorizFrame(\
            [btnEnterURI, ttipEnterURI], condenseMargins=True)
         fromFrameList.append(frmEnterURI)

      ########################################################################
      # In Expert usermode, allow the user to modify source addresses
      if self.main.usermode == USERMODE.Expert:

         sendChangeToFrame = QtWidgets.QFrame()
         sendChangeToLayout = QtWidgets.QGridLayout()
         sendChangeToLayout.addWidget(self.lblChangeAddr,  0,0)
         sendChangeToLayout.addWidget(self.edtChangeAddr,  0,1)
         sendChangeToLayout.addWidget(self.btnChangeAddr,  0,2)
         sendChangeToLayout.addWidget(self.lblAutoDetect,  1,1, 1,2)
         sendChangeToLayout.setColumnStretch(0,0)
         sendChangeToLayout.setColumnStretch(1,1)
         sendChangeToLayout.setColumnStretch(2,0)
         sendChangeToFrame.setLayout(sendChangeToLayout)


         btngrp = QtWidgets.QButtonGroup(self)
         btngrp.addButton(self.radioFeedback)
         btngrp.addButton(self.radioSpecify)
         btngrp.setExclusive(True)
         self.chkDefaultChangeAddr.toggled.connect(self.toggleChngAddr)
         self.radioSpecify.toggled.connect(self.toggleSpecify)
         frmChngLayout = QtWidgets.QGridLayout()
         i = 0
         frmChngLayout.addWidget(self.chkDefaultChangeAddr, i, 0, 1, 6)
         frmChngLayout.addWidget(self.ttipSendChange,       i, 6, 1, 2)
         i += 1
         frmChngLayout.addWidget(self.radioFeedback,        i, 1, 1, 5)
         frmChngLayout.addWidget(self.ttipFeedback,         i, 6, 1, 2)
         i += 1
         frmChngLayout.addWidget(self.radioSpecify,         i, 1, 1, 5)
         frmChngLayout.addWidget(self.ttipSpecify,          i, 6, 1, 2)
         i += 1
         frmChngLayout.addWidget(sendChangeToFrame, i, 1, 1, 6)
         i += 1
         frmChngLayout.addWidget(self.chkRememberChng, i, 1, 1, 7)

         frmChngLayout.addWidget(self.vertLine, 1, 0, i - 1, 1)
         frmChngLayout.setColumnStretch(0,1)
         frmChngLayout.setColumnStretch(1,1)
         frmChngLayout.setColumnStretch(2,1)
         frmChngLayout.setColumnStretch(3,1)
         frmChngLayout.setColumnStretch(4,1)
         frmChngLayout.setColumnStretch(5,1)
         frmChngLayout.setColumnStretch(6,1)
         frmChangeAddr = QtWidgets.QFrame()
         frmChangeAddr.setLayout(frmChngLayout)
         frmChangeAddr.setFrameStyle(STYLE_SUNKEN)
         fromFrameList.append('Stretch')
         fromFrameList.append(frmChangeAddr)
      else:
         fromFrameList.append('Stretch')
      frmBottomLeft = makeVertFrame(fromFrameList, STYLE_RAISED,
         condenseMargins=True)

      lblSend = QRichLabel(self.tr('<b>Sending from Wallet:</b>'))
      lblSend.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignBottom)


      leftFrame = makeVertFrame([lblSend, frmBottomLeft], condenseMargins=True)
      rightFrame = makeVertFrame(\
         [lblRecip, self.scrollRecipArea, txFrm, metaFrm, buttonFrame],
         condenseMargins=True)
      layout = QtWidgets.QHBoxLayout()
      layout.addWidget(leftFrame, 0)
      layout.addWidget(rightFrame, 1)
      layout.setContentsMargins(0,0,0,0)
      layout.setSpacing(0)
      self.setLayout(layout)

      self.makeRecipFrame(1)
      self.setWindowTitle(self.tr('Send Bitcoins'))
      self.setMinimumHeight(self.maxHeight * 20)

      if self.lbox:
         self.toggleSpecify(False)
         self.toggleChngAddr(False)

      hexgeom = TheSettings.get('SendBtcGeometry')
      if len(hexgeom) > 0:
         geom = QtCore.QByteArray(bytes.fromhex(hexgeom))
         self.restoreGeometry(geom)

   # Use this to fire wallet change after the constructor is complete.
   # if it's called during construction then self's container may not exist yet.
   def fireWalletChange(self):
      # Set the wallet in the wallet selector and let all of display components
      # react to it. This is at the end so that we can be sure that all of the
      # components that react to setting the wallet exist.
      if self.lbox:
         self.unsignedCheckbox.setChecked(True)
         self.unsignedCheckbox.setEnabled(False)
      else:
         self.frmSelectedWlt.updateOnWalletChange()

      self.unsignedCheckBoxUpdate()

   #############################################################################
   def unsignedCheckBoxUpdate(self):
      if self.unsignedCheckbox.isChecked():
         self.btnSend.setText(self.tr('Continue'))
         self.btnSend.setToolTip(self.tr('Click to create an unsigned transaction!'))
      else:
         self.btnSend.setText(self.tr('Send!'))
         self.btnSend.setToolTip(self.tr('Click to send bitcoins!'))

   #############################################################################
   def getRBFFlag(self):
      return self.RBFcheckbox.checkState() == QtCore.Qt.Checked

   #############################################################################
   def addOneRecipient(self, addr160, amt, msg, label=None, plainText=None):
      """
      plainText arg can be used, and will override addr160.  It is for 
      injecting either fancy script types, or special keywords into the 
      address field, such as a lockbox ID
      """
      if label is not None and addr160:
         self.wlt.setComment(addr160, label)

      if len(self.widgetTable) > 0:
         lastIsEmpty = True
         for widg in ['QLE_ADDR', 'QLE_AMT', 'QLE_COMM']: 
            if len(str(self.widgetTable[-1][widg].text())) > 0:
               lastIsEmpty = False
      else:
         lastIsEmpty = False

      if not lastIsEmpty:
         self.makeRecipFrame(len(self.widgetTable) + 1)

      if amt:
         amt = coin2str(amt, maxZeros=2).strip()

      if plainText is None:
         plainText = hash160_to_addrStr(addr160)

      self.widgetTable[-1]['QLE_ADDR'].setText(plainText)
      self.widgetTable[-1]['QLE_ADDR'].setCursorPosition(0)
      self.widgetTable[-1]['QLE_AMT'].setText(amt)
      self.widgetTable[-1]['QLE_AMT'].setCursorPosition(0)
      self.widgetTable[-1]['QLE_COMM'].setText(msg)
      self.widgetTable[-1]['QLE_COMM'].setCursorPosition(0)
      
      self.addCoinSelectionRecipient(len(self.widgetTable) - 1)
      self.resolveCoinSelection()


   #############################################################################
   # Now that the wallet can change in the context of the send dialog, this
   # method is used as a callback for when the wallet changes
   # isDoubleClick is unused - do not accept or close dialog on double click
   def setWallet(self, wlt, isDoubleClick=False):
      self.wlt = wlt
      self.wltID = wlt.uniqueIDB58 if wlt else None
      self.setupCoinSelectionInstance()

      if not TheBDM.getState() == BDM_BLOCKCHAIN_READY:
         self.lblSummaryBal.setText('(available when online)', color='DisableFG')
      if self.main.usermode == USERMODE.Expert:
         # Pre-set values based on settings
         chngBehave = self.main.getWltSetting(self.wltID, 'ChangeBehavior')
         chngAddr = self.main.getWltSetting(self.wltID, 'ChangeAddr')
         if chngBehave == 'Feedback':
            self.chkDefaultChangeAddr.setChecked(True)
            self.radioFeedback.setChecked(True)
            self.radioSpecify.setChecked(False)
            self.toggleChngAddr(True)
            self.chkRememberChng.setChecked(True)
         elif chngBehave == 'Specify':
            self.chkDefaultChangeAddr.setChecked(True)
            self.radioFeedback.setChecked(False)
            self.radioSpecify.setChecked(True)
            self.toggleChngAddr(True)
            if checkAddrStrValid(chngAddr):
               self.edtChangeAddr.setText(chngAddr)
               self.edtChangeAddr.setCursorPosition(0)
               self.chkRememberChng.setChecked(True)
         else:
            # Other option is "NewAddr" but in case there's an error, should run
            # this branch by default
            self.chkDefaultChangeAddr.setChecked(False)
            self.radioFeedback.setChecked(False)
            self.radioSpecify.setChecked(False)
            self.toggleChngAddr(False)

         if (self.chkDefaultChangeAddr.isChecked() and \
            not self.radioFeedback.isChecked() and \
            not self.radioSpecify.isChecked()):
            self.radioFeedback.setChecked(True)
      # If there is a unsigned then we have a send button and unsigned checkbox to update
      if self.createUnsignedTxCallback:
         self.unsignedCheckbox.setChecked(wlt.watchingOnly)
         self.unsignedCheckbox.setEnabled(not wlt.watchingOnly)
         self.unsignedCheckBoxUpdate()
      if self.selectWltCallback:
         self.selectWltCallback(wlt)

   #############################################################################      
   def setupCoinSelectionInstance(self):
      if self.wlt is None:
         self.coinSelection = None
         return

      self.coinSelection = CoinSelectionInstance(
         self.wlt, TheBDM.getTopBlockHeight())
      self.coinSelection.setup()

      try:
         self.resetCoinSelectionRecipients()
      except:
         pass

   #############################################################################   
   def setupCoinSelectionForLockbox(self, lbox):
      try:
         lbCppWlt = self.main.cppLockboxWltMap[lbox.uniqueIDB58]
         self.coinSelection = Cpp.CoinSelectionInstance(\
            lbCppWlt, lbox.M, lbox.N, \
            TheBDM.getTopBlockHeight(), lbCppWlt.getSpendableBalance())

      except:
         self.coinSelection = None
      
   #############################################################################
   def resetCoinSelectionRecipients(self):

      if self.coinSelection is None:
         return

      self.coinSelection.resetRecipients()
      for row in range(len(self.widgetTable)):
         self.addCoinSelectionRecipient(row)

      try:
         self.resolveCoinSelection()
      except:
         pass

   #############################################################################
   def addCoinSelectionRecipient(self, id_):

      try:
         coinSelRow = self.widgetTable[id_]
         scrAddr = str(coinSelRow['QLE_ADDR'].text()).strip()
         if len(scrAddr) == 0:
            raise BadAddressError('Empty address string')

         valueStr = str(coinSelRow['QLE_AMT'].text()).strip()
         value = str2coin(valueStr, negAllowed=False)

         self.coinSelection.setRecipient(scrAddr, value, id_)
      except:
         self.resetCoinSelectionText()

   #############################################################################
   def updateCoinSelectionRecipient(self, uid):

      try:
         id_ = -1
         for i in range(len(self.widgetTable)):
            if self.widgetTable[i]['UID'] == uid:
               id_ = i

         if id_ == -1:
            raise Exception()

         coinSelRow = self.widgetTable[id_]
         if 'OP_RETURN' not in coinSelRow:
            addrStr = str(coinSelRow['QLE_ADDR'].text()).strip()
            valueStr = str(coinSelRow['QLE_AMT'].text()).strip()
            try:
               value = str2coin(valueStr, negAllowed=False)
            except:
               value = 0

            self.coinSelection.updateRecipient(addrStr, value, id_)
         else:
            opreturn_message = str(coinSelRow['QLE_ADDR'].text())
            self.coinSelection.updateOpReturnRecipient(id_, opreturn_message)

         self.resolveCoinSelection()

      except:
         self.resetCoinSelectionText()

   #############################################################################
   def serializeUtxoList(self, utxoList):
      serializedUtxoList = []
      for utxo in utxoList:
         serializedUtxoList.append(utxo)

      return serializedUtxoList

   #############################################################################
   def resolveCoinSelection(self):
      maxRecipientID = self.getMaxRecipientID()
      if maxRecipientID != None:
         self.setMaximum(maxRecipientID)

      try:
         fee, feePerByte, adjust_fee = self.feeDialog.getFeeData()
         processFlag = 0
         if self.useCustomListInFull:
            processFlag += CS_USE_FULL_CUSTOM_LIST

         if adjust_fee:
            processFlag += CS_ADJUST_FEE

         if self.shuffleEntries:
            processFlag += CS_SHUFFLE_ENTRIES

         if self.customUtxoList is None or len(self.customUtxoList) == 0:
            self.coinSelection.selectUTXOs(fee, feePerByte, processFlag)
         else:
            self.coinSelection.processCustomUtxoList(\
               self.customUtxoList, fee, feePerByte, processFlag)

         self.feeDialog.updateLabelButton()
      except RuntimeError as e:
         print (f"[resolveCoinSelection] failed with error: {str(e)}")
         self.resetCoinSelectionText()
         raise e

   #############################################################################
   def getCoinSelectionState(self):
      txSize = self.coinSelection.getSizeEstimate()
      flatFee = self.coinSelection.getFlatFee()
      feeByte = self.coinSelection.getFeeByte()

      return txSize, flatFee, feeByte

   #############################################################################
   def resetCoinSelectionText(self):
      self.feeDialog.resetLabel()

   #############################################################################
   # Update the available source address list and balance based on results from
   # coin control. This callback is now necessary because coin control was moved
   # to the Select Wallet Frame
   def coinControlUpdate(self, customUtxoList, altBalance, useAll):
      self.customUtxoList = customUtxoList
      self.altBalance = altBalance
      self.useCustomListInFull = useAll

      try:
         self.resolveCoinSelection()
      except:
         pass

   #############################################################################
   def RBFupdate(self, rbfList, altBalance, forceVerbose=False):
      self.customUtxoList = rbfList
      self.useCustomListInFull = True
      self.altBalance = altBalance

      try:
         self.resolveCoinSelection()
      except:

         if forceVerbose == False:
            return

         #failed to setup rbf send dialog, maybe the setup cannot cover for 
         #auto fee. let's force the fee to 0 and warn the user
         self.feeDialog.setZeroFee()

         try:
            self.resolveCoinSelection()
            MsgBoxCustom(MSGBOX.Warning, self.tr('RBF value error'), \
            self.tr(
               'You are trying to bump the fee of a broadcasted unconfirmed transaction. '
               'Unfortunately, your transaction lacks the funds to cover the default fee. '
               'Therefore, <b><u>the default fee has currently been set to 0</b></u>.<br><br>'
               'You will have to set the appropriate fee and arrange the transaction spend ' 
               'value manually to successfully double spend this transaction.'
               ), \
            yesStr=self.tr('Ok'))

         except:
            MsgBoxCustom(MSGBOX.Error, self.tr('RBF failure'), \
            self.tr(
               'You are trying to bump the fee of a broadcasted unconfirmed transaction. '
               'The process failed unexpectedly. To double spend your transaction, pick '
               'the relevant output from the RBF Control dialog, found in the Send dialog '
               'in Expert User Mode.') , \
               yesStr=self.tr('Ok'))

   #############################################################################
   def handleCppCoinSelectionExceptions(self):
      try:
         self.coinSelection.rethrow()
      except RecipientReuseException as e:
         addrList = e.getAddresses()
         addrParagraph = '<br>'
         for addrEntry in addrList:
            addrParagraph = addrParagraph + ' - ' + addrEntry + '<br>'

         result = MsgBoxCustom(MSGBOX.Warning, self.tr('Recipient reuse'), \
            self.tr(
               'The transaction you crafted <b>reuses</b> the following recipient address(es):<br>'
               '%s<br>'
               ' The sum of values for this leg of the transaction amounts to %s BTC. There is only'
               ' a total of %s BTC available in UTXOs to fund this leg of the'
               ' transaction without <b>damaging your privacy.</b>'
               '<br><br>In order to meet the full payment, Armory has to make use of extra '
               ' UTXOs, <u>and this will result in privacy loss on chain.</u> <br><br>'
               'To progress beyond this warning, choose Ignore. Otherwise'
               ' the operation will be cancelled.' % \
               (addrParagraph, \
                     coin2str(e.total(), 5, maxZeros=0), \
                     coin2str(e.value(), 5, maxZeros=0))), \
            wCancel=True, yesStr=self.tr('Ignore'), noStr=self.tr('Cancel'))

         if not result:
            return False

      return True

   #############################################################################
   def validateInputsGetUSTX(self, peek=False):

      self.freeOfErrors = True
      scripts = []
      addrList = []
      self.comments = []

      #TODO: fix this
      #if self.handleCppCoinSelectionExceptions() == False:
      #   return

      for row in range(len(self.widgetTable)):
         # Verify validity of address strings
         widget_obj = self.widgetTable[row]
         if 'OP_RETURN' in widget_obj:
            continue

         addrStr = str(widget_obj['QLE_ADDR'].text()).strip()
         self.widgetTable[row]['QLE_ADDR'].setText(addrStr) # overwrite w/ stripped
         addrIsValid = True
         addrList.append(addrStr)
         try:
            enteredScript = widget_obj['FUNC_GETSCRIPT']()['Script']
            if not enteredScript:
               addrIsValid = False
            else:
               scripts.append(enteredScript)
         except:
            LOGEXCEPT('Failed to parse entered address: %s', addrStr)
            addrIsValid = False

         if not addrIsValid:
            scripts.append('')
            self.freeOfErrors = False
            self.updateAddrColor(row, Colors.SlightRed)


      numChkFail = sum([1 if len(b)==0 else 0 for b in scripts])
      if not self.freeOfErrors:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Address'),
               self.tr("You have entered %s invalid addresses. "
                       "The errors have been highlighted on the entry screen" % str(numChkFail)), QtWidgets.QMessageBox.Ok)

         for row in range(len(self.widgetTable)):
            try:
               atype, a160 = addrStr_to_hash160(addrList[row]) 
               if atype == -1 or not atype in [ADDRBYTE,P2SHBYTE]:
                  net = 'Unknown Network'
                  if addrList[row][0] in NETWORKS:
                     net = NETWORKS[addrList[row][0]]
                  QtWidgets.QMessageBox.warning(self, self.tr('Wrong Network!'), self.tr(
                     'Address %d is for the wrong network!  You are on the <b>%s</b> '
                     'and the address you supplied is for the the <b>%s</b>!' % (row+1, NETWORKS[ADDRBYTE], net)), QtWidgets.QMessageBox.Ok)
            except:
               pass

         return False

      # Construct recipValuePairs and check that all metrics check out
      scriptValPairs = []
      opreturn_list = []
      totalSend = 0
      for row in range(len(self.widgetTable)):
         widget_obj = self.widgetTable[row]
         if 'OP_RETURN' in widget_obj:
            opreturn_msg = str(widget_obj['QLE_ADDR'].text())
            if len(opreturn_msg) > 80:
               self.updateAddrColor(row, Colors.SlightRed)
               QtWidgets.QMessageBox.critical(self, self.tr('Negative Value'), \
                  self.tr('You have specified a OP_RETURN message over 80 bytes long in recipient %d!' % \
                          (row + 1)), QtWidgets.QMessageBox.Ok)
               return False

            opreturn_list.append(opreturn_msg)
            continue

         try:
            valueStr = str(self.widgetTable[row]['QLE_AMT'].text()).strip()
            value = str2coin(valueStr, negAllowed=False)
            if value == 0:
               QtWidgets.QMessageBox.critical(self, self.tr('Zero Amount'), \
                  self.tr('You cannot send 0 BTC to any recipients.  <br>Please enter '
                  'a positive amount for recipient %d.' % (row+1)), QtWidgets.QMessageBox.Ok)
               return False

         except NegativeValueError:
            QtWidgets.QMessageBox.critical(self, self.tr('Negative Value'), \
               self.tr('You have specified a negative amount for recipient %d. <br>Only '
               'positive values are allowed!.' % (row + 1)), QtWidgets.QMessageBox.Ok)
            return False
         except TooMuchPrecisionError:
            QtWidgets.QMessageBox.critical(self, self.tr('Too much precision'), \
               self.tr('Bitcoins can only be specified down to 8 decimal places. '
               'The smallest value that can be sent is  0.0000 0001 BTC. '
               'Please enter a new amount for recipient %d.' % (row + 1)), QtWidgets.QMessageBox.Ok)
            return False
         except ValueError:
            QtWidgets.QMessageBox.critical(self, self.tr('Missing recipient amount'), \
               self.tr('You did not specify an amount to send!'), QtWidgets.QMessageBox.Ok)
            return False
         except:
            QtWidgets.QMessageBox.critical(self, self.tr('Invalid Value String'), \
               self.tr('The amount you specified '
               'to send to address %d is invalid (%s).' % (row + 1, valueStr)), QtWidgets.QMessageBox.Ok)
            LOGERROR('Invalid amount specified: "%s"', valueStr)
            return False

         totalSend += value

         script = self.widgetTable[row]['FUNC_GETSCRIPT']()['Script']
         scriptValPairs.append([script, value])
         self.comments.append((str(self.widgetTable[row]['QLE_COMM'].text()), value))

      try:
         utxoSelect = self.getUsableTxOutList()
      except RuntimeError as e:
         QtWidgets.QMessageBox.critical(self, self.tr('Coin Selection Failure'), \
               self.tr('Coin selection failed with error: <b>%s<b/>' % e.message), \
               QtWidgets.QMessageBox.Ok)
         return False

      fee = self.coinSelection.getFlatFee()
      fee_byte = self.coinSelection.getFeeByte()

      # Warn user of excessive fee specified
      if peek == False:
         feebyteStr = "%.2f" % fee_byte
         if fee_byte > 10 * MIN_FEE_BYTE:
            reply = QtWidgets.QMessageBox.warning(self, self.tr('Excessive Fee'), self.tr(
               'Your transaction comes with a fee rate of <b>%s satoshis per byte</b>. '
               '</br></br> '
               'This is at least an order of magnitude higher than the minimum suggested fee rate of <b>%s satoshi/Byte</b>. '
               '<br><br>'
               'Are you <i>absolutely sure</i> that you want to send with this '
               'fee? If you do not want to proceed with this fee rate, click "No".' % \
                  (feebyteStr, str(MIN_FEE_BYTE))), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)

            if not reply==QtWidgets.QMessageBox.Yes:
               return False

         elif fee_byte < MIN_FEE_BYTE:
            reply = QtWidgets.QMessageBox.warning(self, self.tr('Insufficient Fee'), self.tr(
               'Your transaction comes with a fee rate of <b>%s satoshi/Byte</b>. '
               '</br><br> '
               'This is lower than the suggested minimum fee rate of <b>%s satoshi/Byte</b>. '
               '<br><br>'
               'Are you <i>absolutely sure</i> that you want to send with this '
               'fee? If you do not want to proceed with this fee rate, click "No".' % \
                  (feebyteStr, str(MIN_FEE_BYTE))), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)

            if not reply==QtWidgets.QMessageBox.Yes:
               return False

      if len(utxoSelect) == 0:
         QtWidgets.QMessageBox.critical(self, self.tr('Coin Selection Error'), self.tr(
            'There was an error constructing your transaction, due to a '
            'quirk in the way Bitcoin transactions work.  If you see this '
            'error more than once, try sending your BTC in two or more '
            'separate transactions.'), QtWidgets.QMessageBox.Ok)
         return False

      # ## IF we got here, everything is good to go...
      #   Just need to get a change address and then construct the tx
      totalTxSelect = sum([u.getValue() for u in utxoSelect])
      totalChange = totalTxSelect - (totalSend + fee)

      self.changeScript = ''
      self.selectedBehavior = ''
      if totalChange > 0:
         script,behavior = self.determineChangeScript(
            utxoSelect, scriptValPairs, peek)
         self.changeScript = script
         self.selectedBehavior = behavior
         scriptValPairs.append([self.changeScript, totalChange])
         LOGINFO('Change address behavior: %s', self.selectedBehavior)
      else:
         self.selectedBehavior = NO_CHANGE

      # Keep a copy of the originally-sorted list for display
      origSVPairs = scriptValPairs[:]

      # Anonymize the outputs
      if self.shuffleEntries:
         random.shuffle(scriptValPairs)

      p2shMap = {}
      pubKeyMap = {}
      
      if self.getRBFFlag():
         for utxo in utxoSelect:
            if utxo.sequence == 2**32 - 1:
               utxo.sequence = 2**32 - 3

      # In order to create the USTXI objects, need to make sure we supply a
      # map of public keys that can be included
      if self.lbox:
         p2shMap = self.lbox.getScriptDict()
         ustx = UnsignedTransaction().createFromTxOutSelection( \
                                       utxoSelect, scriptValPairs, \
                                       p2shMap = p2shMap, \
                                       lockTime=TheBDM.getTopBlockHeight())

         for i in range(len(ustx.ustxInputs)):
            ustx.ustxInputs[i].contribID = self.lbox.uniqueIDB58

         for i in range(len(ustx.decorTxOuts)):
            if ustx.decorTxOuts[i].binScript == self.lbox.binScript:
               ustx.decorTxOuts[i].contribID = self.lbox.uniqueIDB58

      else:
         # If this has nothing to do with lockboxes, we need to make sure
         # we're providing a key map for the inputs

         '''
         for utxo in utxoSelect:
            scrType = getTxOutScriptType(utxo.getScript())
            scrAddr = utxo.getRecipientScrAddr()
            addrObj = self.wlt.getAddrByHash(scrAddr)
            if scrType in CPP_TXOUT_STDSINGLESIG:
               if addrObj:
                  pubKeyMap[scrAddr] = addrObj.getPubKey()
            elif scrType == CPP_TXOUT_P2SH:
               p2shScript = addrObj.getPrecursorScript()
               p2shKey = binary_to_hex(addrObj.getPrefixedAddr())
               p2shMap[p2shKey]  = p2shScript

               addrIndex = addrObj.chainIndex
               try:
                  addrStr = self.wlt.chainIndexMap[addrIndex]
               except:
                  if addrIndex < -2:
                     importIndex = self.wlt.cppWallet.convertToImportIndex(addrIndex)
                     addrStr = self.wlt.linearAddr160List[importIndex]
                  else:
                     raise Exception("invalid address index")

               pubKeyMap[scrAddr] = addrObj.getPubKey()
         '''

         '''
         If we are consuming any number of SegWit utxos, pass the utxo selection
         and outputs to the new signer for processing instead of creating the
         unsigned tx in Python.
         '''

         # Now create the unsigned USTX
         ustx = UnsignedTransaction().createFromTxOutSelection(\
            utxoSelect, scriptValPairs, {}, {}, \
            lockTime=TheBDM.getTopBlockHeight())

         for msg in opreturn_list:
            ustx.addOpReturnOutput(str(msg))

         #resolve signer before returning it
         ustx.resolveSigner(self.wlt.uniqueIDB58)

      txValues = [totalSend, fee, totalChange]
      if not peek:
         if not self.unsignedCheckbox.isChecked():
            dlg = DlgConfirmSend(
               self.wlt, origSVPairs, txValues[1], self, self.main, True, ustx)

            if not dlg.exec_():
               return False
         else:
            self.main.warnNewUSTXFormat()

      return ustx

   def createTxAndBroadcast(self):
      # The Send! button is clicked validate and broadcast tx
      ustx = self.validateInputsGetUSTX()

      if ustx:
         self.updateUserComments()

         if self.createUnsignedTxCallback and self.unsignedCheckbox.isChecked():
            self.createUnsignedTxCallback(ustx)
         else:
            try:
               self.wlt.mainWnd = self.main
               self.wlt.parent = self

               commentStr = ''
               if len(self.comments) == 1:
                  commentStr = self.comments[0][0]
               else:
                  for i in range(len(self.comments)):
                     amt = self.comments[i][1]
                     if len(self.comments[i][0].strip()) > 0:
                        commentStr += '%s (%s);  ' % (self.comments[i][0], coin2str_approx(amt).strip())

               def finalizeSignTx(success):
                  #this needs to run in the GUI thread
                  def signTxLastStep(success):
                     if success:
                        finalTx = ustx.getSignedPyTx()

                        if len(commentStr) > 0:
                           self.wlt.setComment(finalTx.getHash(), commentStr)
                        self.main.broadcastTransaction(finalTx)

                        if self.sendCallback:
                           self.sendCallback()
                     else:
                        QtWidgets.QMessageBox.warning(self, self.tr('Error'),
                           self.tr('Failed to sign transaction!'),
                           QtWidgets.QMessageBox.Ok)
                  TheSignalExecution.executeMethod(signTxLastStep, success)

               ustx.signTx(self.wlt.uniqueIDB58, finalizeSignTx, self)

            except:
               LOGEXCEPT('Problem sending transaction!')
               # TODO: not sure what errors to catch here, yet...
               raise

   #############################################################################
   def getUsableBalance(self):
      if self.lbox is None:
         if self.altBalance == None:
            return self.wlt.getBalance('Spendable')
         else:
            return self.altBalance
      else:
         lbID = self.lbox.uniqueIDB58
         cppWlt = self.main.cppLockboxWltMap.get(lbID)
         if cppWlt is None:
            LOGERROR('Somehow failed to get cppWlt for lockbox: %s', lbID)

         return cppWlt.getSpendableBalance()

   #############################################################################
   def getUsableTxOutList(self):
      self.resolveCoinSelection()
      utxoVec = self.coinSelection.getUtxoSelection()
      utxoSelect = []
      for utxo in utxoVec:
         pyUtxo = PyUnspentTxOut().createFromBridgeUtxo(utxo)
         utxoSelect.append(pyUtxo)
      return utxoSelect

   #############################################################################
   def warnChangeTypeMismatch(self, changeType, outputAddressTypes):
      changeTypeStr = getNameForAddrType(changeType)

      txAddrTypesDescr = "<pre>"
      for addrType in outputAddressTypes:
         txAddrTypesDescr += getNameForAddrType(addrType) + "<br>"
      txAddrTypesDescr += "</pre>"

      QtWidgets.QMessageBox.warning(self, self.tr('Change address type mismatch'),
         self.tr("Could not find a change address type that matches the "
            "outputs in this transaction.<br>"

            "The output types are:<br>%s"

            "<br>The selected change type is <b>%s.</b>"
            "<br><br>"

            "If sent as such, this transaction can damage your privacy. You "
            "should consider creating this transaction from a wallet with "
            "address types that match your recipients."
            % (txAddrTypesDescr, changeTypeStr)),
         QtWidgets.QMessageBox.Ok)

   #############################################################################
   def getDefaultChangeAddress(self, scriptValPairs, peek):
      if len(scriptValPairs) == 0:
         raise Exception("cannot calculate change without at least one recipient")

      def getAddr(typeStr):
         typeInt = AddressEntryType_Default
         if typeStr == 'P2PKH':
            typeInt = AddressEntryType_P2PKH
         elif typeStr == 'P2SH-P2WPKH':
            typeInt = AddressEntryType_P2SH + AddressEntryType_P2WPKH
         elif typeStr == 'P2SH-P2PK':
            typeInt = AddressEntryType_P2SH + AddressEntryType_P2PK

         if peek:
            addrObj = self.wlt.peekChangeAddr(typeInt)
         else:
            addrObj = self.wlt.getNewChangeAddr(typeInt)
         return addrObj

      changeType = self.wlt.getDefaultChangeType()

      #get address type for each of our outputs
      outputAddressTypes = set()
      for i in range(0, len(scriptValPairs)):
         svPair = scriptValPairs[i]
         scriptType = TheBridge.scriptUtils.getTxOutScriptType(svPair[0])
         try:
            addrType = getAddressTypeForOutputType(scriptType)
            outputAddressTypes.add(addrType)
         except:
            continue

      #resolve change type if set to default
      effectiveType = changeType
      if effectiveType == AddressEntryType_Default:
         effectiveType = self.wlt.getDefaultAddressType()

      #does out change type match any of the output types?
      if not addrTypeInSet(effectiveType, outputAddressTypes):
         if changeType == AddressEntryType_Default:
            #if the change type is set to default, let's try to find
            #an eligible address type amoung our output types
            wltAddrTypeSet = self.wlt.getAddressTypes()
            for addrType in wltAddrTypeSet:
               if addrTypeInSet(addrType, outputAddressTypes):
                  #found an output type that matches the eligible address
                  #types of our wallet, let's use it for our change type
                  effectiveType = addrType
                  break

      #if we have failed to match the change type to our outputs,
      #warn the user and continue
      if not addrTypeInSet(effectiveType, outputAddressTypes):
         self.warnChangeTypeMismatch(effectiveType, outputAddressTypes)

      return getAddr(effectiveType)


   #############################################################################
   def determineChangeScript(self, utxoList, scriptValPairs, peek=False):
      changeScript = ''
      changeAddrStr = ''
      changeAddr160 = ''

      selectedBehavior = 'NewAddr' if self.lbox is None else 'Feedback'

      if not self.main.usermode == USERMODE.Expert or \
         not self.chkDefaultChangeAddr.isChecked():
         # Default behavior for regular wallets is 'NewAddr', but for lockboxes
         # the default behavior is "Feedback" (send back to the original addr
         if self.lbox is None:
            changeAddrObj = self.getDefaultChangeAddress(scriptValPairs, peek)
            changeAddr160 = changeAddrObj.getAddr160()
            changeScript  = TheBridge.scriptUtils.getTxOutScriptForScrAddr(\
               changeAddrObj.getPrefixedAddr())
            self.wlt.setComment(changeAddr160, CHANGE_ADDR_DESCR_STRING)
         else:
            changeScript  = self.lbox.getScript()

      if self.main.usermode == USERMODE.Expert:
         if not self.chkDefaultChangeAddr.isChecked():
            self.main.setWltSetting(self.wltID, 'ChangeBehavior', selectedBehavior)
         else:
            if self.radioFeedback.isChecked():
               selectedBehavior = 'Feedback'
               changeScript = utxoList[0].getScript()
            elif self.radioSpecify.isChecked():
               selectedBehavior = 'Specify'
               changeScript = self.getUserChangeScript()['Script']
               if changeScript is None:
                  QtWidgets.QMessageBox.warning(self, self.tr('Invalid Address'), self.tr(
                     'You specified an invalid change address for this transcation.'), QtWidgets.QMessageBox.Ok)
                  return None
               scrType = getTxOutScriptType(changeScript)
               if scrType in CPP_TXOUT_HAS_ADDRSTR:
                  changeAddrStr = script_to_addrStr(changeScript)
               elif scrType==CPP_TXOUT_MULTISIG:
                  scrP2SH = script_to_p2sh_script(changeScript)
                  changeAddrStr = script_to_addrStr(scrP2SH)

      if self.main.usermode == USERMODE.Expert and self.chkRememberChng.isChecked():
         self.main.setWltSetting(self.wltID, 'ChangeBehavior', selectedBehavior)
         if selectedBehavior == 'Specify' and len(changeAddrStr) > 0:
            self.main.setWltSetting(self.wltID, 'ChangeAddr', changeAddrStr)
      else:
         self.main.setWltSetting(self.wltID, 'ChangeBehavior', 'NewAddr')

      return changeScript,selectedBehavior

   #####################################################################
   def getMaxRecipientID(self):
      for widget_obj in self.widgetTable:
         if 'OP_RETURN' in widget_obj:
            continue

         if widget_obj['BTN_MAX'].isChecked():
            return widget_obj['UID']
      return None

   #####################################################################
   def setMaximum(self, targWidgetID):
      #is the box checked?
      targetWidget = None
      for widget_obj in self.widgetTable:
         if widget_obj['UID'] == targWidgetID:
            targetWidget = widget_obj

      if targetWidget != None and targetWidget['BTN_MAX'].isChecked():
         #disable all check boxes but this one
         for widget_obj in self.widgetTable:
            if 'BTN_MAX' in widget_obj:
               widget_obj['BTN_MAX'].setEnabled(False)

         targetWidget['BTN_MAX'].setEnabled(True)
         targetWidget['QLE_AMT'].setEnabled(False)
      else:
         #enable all checkboxes and return
         for widget_obj in self.widgetTable:
            if 'BTN_MAX' in widget_obj:
               widget_obj['BTN_MAX'].setEnabled(True)
               widget_obj['QLE_AMT'].setEnabled(True)
         return

      nRecip = len(self.widgetTable)
      totalOther = 0
      r = 0
      try:
         bal = self.getUsableBalance()
         txFee, fee_byte, adjust = self.feeDialog.getFeeData()
         while r < nRecip:
            # Use while loop so 'r' is still in scope in the except-clause
            if targWidgetID == self.widgetTable[r]['UID']:
               r += 1
               continue

            if 'OP_RETURN' in self.widgetTable[r]:
               r += 1
               continue

            amtStr = str(self.widgetTable[r]['QLE_AMT'].text()).strip()
            if len(amtStr) > 0:
               totalOther += str2coin(amtStr)
            r += 1

         if txFee == 0 and fee_byte != 0:
            if self.customUtxoList != None and len(self.customUtxoList) > 0:
               serializedUtxoList = self.serializeUtxoList(self.customUtxoList)
               txFee = self.coinSelection.getFeeForMaxValUtxoVector(serializedUtxoList, fee_byte)
            else:
               txFee = self.coinSelection.getFeeForMaxVal(fee_byte)

      except:
         QtWidgets.QMessageBox.warning(self, self.tr('Invalid Input'), \
               self.tr('Cannot compute the maximum amount '
               'because there is an error in the amount '
               'for recipient %s.' % (r + 1)), QtWidgets.QMessageBox.Ok)
         return

      maxStr = coin2str((bal - (txFee + totalOther)), maxZeros=0)
      if bal < txFee + totalOther:
         QtWidgets.QMessageBox.warning(self, self.tr('Insufficient funds'), \
               self.tr('You have specified more than your spendable balance to '
               'the other recipients and the transaction fee.  Therefore, the '
               'maximum amount for this recipient would actually be negative.'), \
               QtWidgets.QMessageBox.Ok)
         return

      targetWidget['QLE_AMT'].setText(maxStr.strip())
      self.isMax = True


   #####################################################################
   def createSetMaxButton(self, targWidgetID):
      newBtn = QtWidgets.QCheckBox('MAX')
      #newBtn.setMaximumWidth(relaxedSizeStr(self, 'MAX')[0])
      newBtn.setToolTip(self.tr('Fills in the maximum spendable amount minus '
                         'the amounts specified for other recipients '
                         'and the transaction fee '))
      funcSetMax = lambda:  self.setMaximum(targWidgetID)
      newBtn.clicked.connect(funcSetMax)
      return newBtn


   #####################################################################
   def makeRecipFrame(self, nRecip, is_opreturn=False):

      frmRecip = QtWidgets.QFrame()
      frmRecip.setFrameStyle(QtWidgets.QFrame.NoFrame)
      frmRecipLayout = QtWidgets.QVBoxLayout()


      def recipientAddrChanged(widget_obj):
         def callbk():
            self.updateWidgetAddrColor(widget_obj, Colors.Background)
            self.updateCoinSelectionRecipient(widget_obj['UID'])
         return callbk

      def recipientValueChanged(uid):
         def callbk():
            self.updateCoinSelectionRecipient(uid)
         return callbk

      def createAddrWidget(widget_obj, r):
         widget_obj['LBL_ADDR'] = QtWidgets.QLabel('Address %d:' % (r+1))

         addrEntryWidgets = self.main.createAddressEntryWidgets(self, maxDetectLen=45, boldDetectParts=1)
         widget_obj['FUNC_GETSCRIPT'] = addrEntryWidgets['CALLBACK_GETSCRIPT']
         widget_obj['QLE_ADDR'] = addrEntryWidgets['QLE_ADDR']
         widget_obj['QLE_ADDR'].setMinimumWidth(relaxedSizeNChar(GETFONT('var'), 20)[0])
         widget_obj['QLE_ADDR'].setMaximumHeight(self.maxHeight)
         widget_obj['QLE_ADDR'].setFont(GETFONT('var', 9))

         widget_obj['QLE_ADDR'].textChanged.connect(\
            recipientAddrChanged(widget_obj))

         widget_obj['BTN_BOOK'] = addrEntryWidgets['BTN_BOOK']
         widget_obj['LBL_DETECT'] = addrEntryWidgets['LBL_DETECT']
   
         widget_obj['LBL_AMT'] = QtWidgets.QLabel('Amount:')
         widget_obj['QLE_AMT'] = QtWidgets.QLineEdit()
         widget_obj['QLE_AMT'].setFont(GETFONT('Fixed'))
         widget_obj['QLE_AMT'].setMinimumWidth(tightSizeNChar(GETFONT('Fixed'), 14)[0])
         widget_obj['QLE_AMT'].setMaximumHeight(self.maxHeight)
         widget_obj['QLE_AMT'].setAlignment(QtCore.Qt.AlignLeft)

         widget_obj['QLE_AMT'].textChanged.connect(\
            recipientValueChanged(widget_obj['UID']))

         widget_obj['LBL_BTC'] = QtWidgets.QLabel('BTC')
         widget_obj['LBL_BTC'].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)

         widget_obj['BTN_MAX'] = \
                           self.createSetMaxButton(widget_obj['UID'])

         widget_obj['LBL_COMM'] = QtWidgets.QLabel('Comment:')
         widget_obj['QLE_COMM'] = QtWidgets.QLineEdit()
         widget_obj['QLE_COMM'].setFont(GETFONT('var', 9))
         widget_obj['QLE_COMM'].setMaximumHeight(self.maxHeight)
         widget_obj['QLE_COMM'].setMaxLength(MAX_COMMENT_LENGTH)    


      def opReturnMessageChanged(widget_obj):
         def callbk():
            self.updateCoinSelectionRecipient(widget_obj['UID'])
         return callbk

      def createOpReturnWidget(widget_obj):     
         widget_obj['LBL_ADDR'] = QtWidgets.QLabel('OP_RETURN Message:')
         widget_obj['QLE_ADDR'] = QtWidgets.QLineEdit()
         widget_obj['OP_RETURN'] = ""

         widget_obj['QLE_ADDR'].textChanged.connect(\
            recipientAddrChanged(widget_obj))

      recip_diff = nRecip - len(self.widgetTable)
      if recip_diff > 0:
         for i in range(recip_diff):
            r = len(self.widgetTable)
            self.widgetTable.append({})

            self.widgetTable[r]['UID'] = TheBridge.utils.generateRandomHex(8)

            if not is_opreturn:
               createAddrWidget(self.widgetTable[r], r)
            else:
               createOpReturnWidget(self.widgetTable[r])

      else:
         self.widgetTable = self.widgetTable[0:len(self.widgetTable) + recip_diff]

      for widget_obj in self.widgetTable:

         subfrm = QtWidgets.QFrame()
         subfrm.setFrameStyle(STYLE_RAISED)
         subLayout = QtWidgets.QGridLayout()
         subLayout.addWidget(widget_obj['LBL_ADDR'],  0,0, 1,1)
         subLayout.addWidget(widget_obj['QLE_ADDR'],  0,1, 1,5)
         try:
            subLayout.addWidget(widget_obj['BTN_BOOK'],  0,6, 1,1)

            subLayout.addWidget(widget_obj['LBL_DETECT'], 1,1, 1,6)

            subLayout.addWidget(widget_obj['LBL_AMT'],   2,0, 1,1)
            subLayout.addWidget(widget_obj['QLE_AMT'],   2,1, 1,2)
            subLayout.addWidget(widget_obj['LBL_BTC'],   2,3, 1,1)
            subLayout.addWidget(widget_obj['BTN_MAX'],   2,4, 1,1)
            subLayout.addWidget(QtWidgets.QLabel(''), 2, 5, 1, 2)

            subLayout.addWidget(widget_obj['LBL_COMM'],  3,0, 1,1)
            subLayout.addWidget(widget_obj['QLE_COMM'],  3,1, 1,6)
         except:
            pass

         subLayout.setContentsMargins(5, 5, 5, 5)
         subLayout.setSpacing(3)
         subfrm.setLayout(subLayout)

         frmRecipLayout.addWidget(subfrm)


      btnFrm = QtWidgets.QFrame()
      btnFrm.setFrameStyle(QtWidgets.QFrame.NoFrame)
      btnLayout = QtWidgets.QHBoxLayout()
      lbtnAddRecip = QLabelButton(self.tr('+ Recipient'))
      lbtnAddRecip.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)  
      lbtnRmRecip = QLabelButton(self.tr('- Recipient'))
      lbtnRmRecip.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      lbtnAddRecip.linkActivated.connect(lambda: self.makeRecipFrame(nRecip + 1))
      lbtnRmRecip.linkActivated.connect(lambda: self.makeRecipFrame(nRecip - 1))

      if self.main.usermode == USERMODE.Expert:
         lbtnAddOpReturn = QLabelButton('+ OP_RETURN')
         lbtnAddOpReturn.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         lbtnAddOpReturn.linkActivated.connect(\
            lambda: self.makeRecipFrame(nRecip + 1, True))

      btnLayout.addStretch()
      btnLayout.addWidget(lbtnAddRecip)
      if self.main.usermode == USERMODE.Expert:
         btnLayout.addWidget(lbtnAddOpReturn)
      btnLayout.addWidget(lbtnRmRecip)
      btnFrm.setLayout(btnLayout)

      frmRecipLayout.addWidget(btnFrm)
      frmRecipLayout.addStretch()
      frmRecip.setLayout(frmRecipLayout)
      self.scrollRecipArea.setWidget(frmRecip)
      self.scrollRecipArea.setWidgetResizable(True)

      if recip_diff < 0:
         self.resetCoinSelectionRecipients()

   #############################################################################
   def clickEnterURI(self):
      dlg = DlgUriCopyAndPaste(self.parent(), self.main)
      dlg.exec_()

      if len(dlg.uriDict) > 0:
         lastIsEmpty = True
         for widg in ['QLE_ADDR', 'QLE_AMT', 'QLE_COMM']: 
            if len(str(self.widgetTable[-1][widg].text())) > 0:
               lastIsEmpty = False

         if not lastIsEmpty:
            self.makeRecipFrame(len(self.widgetTable) + 1)

         self.widgetTable[-1]['QLE_ADDR'].setText(dlg.uriDict['address'])
         if 'amount' in dlg.uriDict:
            amtStr = coin2str(dlg.uriDict['amount'], maxZeros=1).strip()
            self.widgetTable[-1]['QLE_AMT'].setText(amtStr)


         haveLbl = 'label' in dlg.uriDict
         haveMsg = 'message' in dlg.uriDict

         dispComment = ''
         if haveLbl and haveMsg:
            dispComment = dlg.uriDict['label'] + ': ' + dlg.uriDict['message']
         elif not haveLbl and haveMsg:
            dispComment = dlg.uriDict['message']
         elif haveLbl and not haveMsg:
            dispComment = dlg.uriDict['label']

         self.widgetTable[-1]['QLE_COMM'].setText(dispComment)


   #############################################################################
   def toggleSpecify(self, b):
      self.lblChangeAddr.setVisible(b)
      self.edtChangeAddr.setVisible(b)
      self.btnChangeAddr.setVisible(b)
      self.lblAutoDetect.setVisible(b)

   #############################################################################
   def toggleChngAddr(self, b):
      self.radioFeedback.setVisible(b)
      self.radioSpecify.setVisible(b)
      self.ttipFeedback.setVisible(b)
      self.ttipSpecify.setVisible(b)
      self.chkRememberChng.setVisible(b)
      self.lblAutoDetect.setVisible(b)
      self.vertLine.setVisible(b)
      if not self.radioFeedback.isChecked() and not self.radioSpecify.isChecked():
         self.radioFeedback.setChecked(True)
      self.toggleSpecify(b and self.radioSpecify.isChecked())

   #############################################################################
   def updateWidgetAddrColor(self, widget, color):
      palette = QtGui.QPalette()
      palette.setColor(QtGui.QPalette.Base, color)
      widget['QLE_ADDR'].setPalette(palette)
      widget['QLE_ADDR'].setAutoFillBackground(True)

   #############################################################################
   def updateAddrColor(self, idx, color):
      self.updateWidgetAddrColor(self.widgetTable[idx], color)

   #############################################################################   
   def previewTx(self):
      ustx = self.validateInputsGetUSTX(peek=True)
      if not isinstance(ustx, UnsignedTransaction):
         return

      txDlg = DlgDispTxInfo(ustx, self.wlt, self.parent(), self.main)
      txDlg.exec_()

   #############################################################################      
   def resetRecipients(self):
      self.widgetTable = []

   #############################################################################  
   def prefillFromURI(self, prefill):
      amount = prefill.get('amount','')
      message = prefill.get('message','')
      label = prefill.get('label','')
      if prefill.get('lockbox',''):
         plainStr = createLockboxEntryStr(prefill.get('lockbox',''))
         self.addOneRecipient(None, amount, message, None, plainStr)
      else:
         addrStr = prefill.get('address','')
         atype, addr160 = addrStr_to_hash160(addrStr)
         if atype == getAddrByte():
            self.addOneRecipient(addr160, amount, message, label)
         else:
            self.addOneRecipient(None, amount, message, label, plainText=addrStr)

   ############################################################################# 
   def prefillFromBatch(self, txBatchStr):
      batch = TransactionBatch()
      batch.processBatchStr(txBatchStr)

      prefillData = {}
      
      walletID = batch.getWalletID()
      prefillData['walletID'] = walletID

      prefillData['recipients'] = []
      rcpDict = prefillData['recipients']
      recipients = batch.getRecipients()
      recCount = len(recipients)
      for rcp in recipients:
         rcpDict.append([rcp.address_, rcp.value_, rcp.comment_])

      spenders = batch.getSpenders()
      if len(spenders) > 0:
         prefillData['spenders'] = []
         spdDict = prefillData['spenders']
         for spd in spenders:
            spdDict.append([spd.txHash_, spd.index_, spd.sequence_])

      changeAddr = batch.getChange().address_
      if len(changeAddr) > 0:
         prefillData['change'] = changeAddr

      fee_rate = batch.getFeeRate()
      if fee_rate != 0:
         prefillData['fee_rate'] = fee_rate

      flat_fee = batch.getFlatFee()
      if flat_fee != 0:
         prefillData['flat_fee'] = flat_fee

      self.prefill(prefillData)

   #############################################################################      
   def prefill(self, prefill):
      '''
      format:
      {
      walleID:str,
      recipients:[[b58addr, value, comment], ...],
      spenders:[[txHashStr, txOutID, seq], ...],
      change:b58addr,
      fee_rate:integer,
      flat_fee:float
      }
      '''

      #reset recipients
      self.resetRecipients()

      #wallet
      try:
         wltid = prefill['walletID']
         comboIndex = self.wltIDList.index(wltid)
         self.frmSelectedWlt.walletComboBox.setCurrentIndex(comboIndex)
         self.fireWalletChange()
      except:
         pass

      #recipients
      recipients = prefill['recipients']
      for rpt in recipients:
         addrStr = rpt[0]
         value = rpt[1]

         comment = ""
         if len(rpt) == 3:
            comment = rpt[2]

         hash160 = None
         try:
            prefix, hash160 = addrStr_to_hash160(addrStr)
         except:
            pass

         try:
            self.addOneRecipient(hash160, value, comment, plainText=addrStr)
         except:
            pass

      try:
         self.resetCoinSelectionRecipients()
      except:
         pass

      #do not shuffle outputs on batches
      self.shuffleEntries = False

      #change
      try:
         changeAddr = prefill['change']
         self.chkDefaultChangeAddr.setChecked(True)
         self.radioSpecify.setChecked(True)
         self.edtChangeAddr.setText(changeAddr)
      except:
         pass

      #fee

      #spenders
      spenders = prefill['spenders']

      def findUtxo(utxoList):
         utxoDict = {}
         for utxo in utxoList:
            txhashstr = utxo.getTxHashStr()
            if not txhashstr in utxoDict:
               utxoDict[txhashstr] = {}

            hashDict = utxoDict[txhashstr]
            txoutid = int(utxo.getTxOutIndex())
            hashDict[txoutid] = utxo

         customUtxoList = []
         customBalance = 0 
         for spd in spenders:
            txhashstr = spd[0]
            txoutid = int(spd[1])
            seq = spd[2]

            hashDict = utxoDict[txhashstr]
            utxo = hashDict[txoutid]
            utxo.sequence = seq

            customUtxoList.append(utxo)
            customBalance += utxo.getValue()

         return customUtxoList, customBalance

      try:
         utxolist, balance = findUtxo(self.wlt.getFullUTXOList())
         self.frmSelectedWlt.customUtxoList = utxolist
         self.frmSelectedWlt.altBalance = balance
         self.frmSelectedWlt.useAllCCList = True
         self.frmSelectedWlt.updateOnCoinControl()
      except:
         utxolist, balance = findUtxo(self.wlt.getRBFTxOutList())
         self.frmSelectedWlt.customUtxoList = utxolist
         self.frmSelectedWlt.altBalance = balance
         self.frmSelectedWlt.updateOnRBF(True) 

   #############################################################################
   def updateUserComments(self):
      for row in range(len(self.widgetTable)):
         widget_obj = self.widgetTable[row]
         if 'OP_RETURN' in widget_obj:
            continue

         addr_comment = str(self.widgetTable[row]['QLE_COMM'].text())
         addr_str = str(self.widgetTable[row]['QLE_ADDR'].text())

         try:
            addr160 = addrStr_to_hash160(addr_str)[1]
            self.wlt.setComment(addr160, addr_comment)
         except:
            pass
