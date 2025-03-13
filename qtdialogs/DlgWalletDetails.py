##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2025, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from qtpy import QtCore, QtWidgets

from armoryengine.ArmoryUtils import getVersionString, coin2str, isASCII
from armoryengine.AddressUtils import addrStr_to_hash160
from armoryengine.BDM import TheBDM, BDM_UNINITIALIZED, BDM_OFFLINE, \
   BDM_SCANNING
from armoryengine.Settings import TheSettings
from armoryengine.WalletUtils import WalletTypes, determineWalletType
from armorycolors import htmlColor
from ui.TreeViewGUI import AddressTreeModel

from qtdialogs.qtdefines import USERMODE, \
   relaxedSizeNChar, relaxedSizeStr, QLabelButton, STYLE_SUNKEN, STYLE_NONE, \
   QRichLabel, makeHorizFrame, restoreTableView, \
   WLTFIELDS, tightSizeStr, saveTableView, tightSizeNChar, \
   UnicodeErrorBox, STRETCH, createToolTipWidget, MSGBOX

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.MsgBoxWithDNAA import MsgBoxWithDNAA
from qtdialogs.MsgBoxCustom import MsgBoxCustom
from qtdialogs.DlgSetComment import DlgSetComment

from qtdialogs.qtdialogs import LoadingDisp
from qtdialogs.DlgNewAddress import \
   DlgNewAddressDisp, ShowRecvCoinsWarningIfNecessary
from qtdialogs.DlgKeypoolSettings import DlgKeypoolSettings
from qtdialogs.DlgSendBitcoins import DlgSendBitcoins
from qtdialogs.DlgBackupCenter import DlgBackupCenter
from qtdialogs.DlgAddressInfo import DlgAddressInfo
from qtdialogs.DlgRestore import OpenPaperBackupDialog


################################################################################
class DlgWalletDetails(ArmoryDialog):
   """ For displaying the details of a specific wallet, with options """

   #############################################################################
   def __init__(self, wlt, usermode=USERMODE.Standard, parent=None, main=None):
      super(DlgWalletDetails, self).__init__(parent, main)
      self.setAttribute(QtCore.Qt.WA_DeleteOnClose)

      self.wlt = wlt
      self.usermode = usermode
      self.wlttype, self.typestr = determineWalletType(wlt, parent)
      if self.typestr == 'Encrypted':
         self.typestr = 'Encrypted (AES256)'

      self.labels = [wlt.labelName, wlt.labelDescr]
      self.passphrase = ''
      self.setMinimumSize(800, 400)

      w, h = relaxedSizeNChar(self, 60)
      viewWidth, viewHeight = w, 10 * h

      # Address view
      self.wltAddrTreeModel = AddressTreeModel(self, wlt)
      self.wltAddrView = QtWidgets.QTreeView()
      self.wltAddrView.setModel(self.wltAddrTreeModel)
      self.wltAddrView.setMinimumWidth(550)
      self.wltAddrView.setMinimumHeight(150)
      self.wltAddrView.doubleClicked.connect(self.dblClickAddressView)

      # Now add all the options buttons, dependent on the type of wallet.

      lbtnChangeLabels = QLabelButton(self.tr('Change Wallet Labels'))
      lbtnChangeLabels.linkActivated.connect(self.changeLabels)

      if not self.wlt.watchingOnly:
         s = ''
         if self.wlt.useEncryption:
            s = self.tr('Change or Remove Passphrase')
         else:
            s = self.tr('Encrypt Wallet')
         lbtnChangeCrypto = QLabelButton(s)
         lbtnChangeCrypto.linkActivated.connect(self.changeEncryption)

      exportStr = 'Data' if self.wlt.watchingOnly else 'Copy'
      lbtnSendBtc = QLabelButton(self.tr('Send Bitcoins'))
      lbtnGenAddr = QLabelButton(self.tr('Receive Bitcoins'))
      lbtnImportA = QLabelButton(self.tr('Import/Sweep Private Keys'))
      lbtnDeleteA = QLabelButton(self.tr('Remove Imported Address'))
      lbtnExpWOWlt = QLabelButton(self.tr('Export Watching-Only %s' % exportStr))
      lbtnBackups = QLabelButton(self.tr('<b>Backup This Wallet</b>'))
      lbtnRemove = QLabelButton(self.tr('Delete/Remove Wallet'))

      lbtnSendBtc.linkActivated.connect(self.execSendBtc)
      lbtnGenAddr.linkActivated.connect(self.getNewAddress)
      lbtnBackups.linkActivated.connect(self.execBackupDlg)
      lbtnRemove.linkActivated.connect(self.execRemoveDlg)
      lbtnImportA.linkActivated.connect(self.execImportAddress)
      lbtnDeleteA.linkActivated.connect(self.execDeleteAddress)
      lbtnExpWOWlt.linkActivated.connect(self.execExpWOCopy)

      lbtnSendBtc.setToolTip(self.tr('Send bitcoins to other users, or transfer between wallets'))
      if self.wlt.watchingOnly:
         lbtnSendBtc.setToolTip(self.tr('If you have a full-copy of this wallet on another computer, you can prepare a '
                                'transaction, to be signed by that computer.'))
      lbtnGenAddr.setToolTip(self.tr('Get a new address from this wallet for receiving '
                             'bitcoins.  Right click on the address list below '
                             'to copy an existing address.'))
      lbtnImportA.setToolTip(self.tr('Import or "Sweep" an address which is not part '
                             'of your wallet.  Useful for VanityGen addresses '
                             'and redeeming Casascius physical bitcoins.'))
      lbtnDeleteA.setToolTip(self.tr('Permanently delete an imported address from '
                             'this wallet.  You cannot delete addresses that '
                             'were generated natively by this wallet.'))
      # lbtnSweepA .setToolTip('')
      lbtnExpWOWlt.setToolTip(self.tr('Export a copy of this wallet that can '
                             'only be used for generating addresses and '
                             'monitoring incoming payments.  A watching-only '
                             'wallet cannot spend the funds, and thus cannot '
                             'be compromised by an attacker'))
      lbtnBackups.setToolTip(self.tr('See lots of options for backing up your wallet '
                             'to protect the funds in it.'))
      lbtnRemove.setToolTip(self.tr('Permanently delete this wallet, or just delete '
                             'the private keys to convert it to a watching-only '
                             'wallet.'))
      #lbtnRecover.setToolTip('Attempt to recover a lost password using '
      #                      'details that you remember.')
      if not self.wlt.watchingOnly:
         lbtnChangeCrypto.setToolTip(self.tr('Add/Remove/Change wallet encryption settings.'))

      optFrame = QtWidgets.QFrame()
      optFrame.setFrameStyle(STYLE_SUNKEN)
      optLayout = QtWidgets.QVBoxLayout()

      hasPriv = not self.wlt.watchingOnly
      adv = (self.main.usermode in (USERMODE.Advanced, USERMODE.Expert))

      def createVBoxSeparator():
         frm = QtWidgets.QFrame()
         frm.setFrameStyle(QtWidgets.QFrame.HLine | QtWidgets.QFrame.Plain)
         return frm

      if True:              optLayout.addWidget(lbtnSendBtc)
      if True:              optLayout.addWidget(lbtnGenAddr)
      if hasPriv:           optLayout.addWidget(lbtnChangeCrypto)
      if True:              optLayout.addWidget(lbtnChangeLabels)

      if True:              optLayout.addWidget(createVBoxSeparator())

      if hasPriv:           optLayout.addWidget(lbtnBackups)
      if adv:               optLayout.addWidget(lbtnExpWOWlt)
      if True:              optLayout.addWidget(lbtnRemove)
      # if True:              optLayout.addWidget(lbtnRecover)
      # Not sure yet that we want to include the password finer in here

      if adv:               optLayout.addWidget(createVBoxSeparator())

      if adv:   optLayout.addWidget(lbtnImportA)
      if hasPriv and adv:   optLayout.addWidget(lbtnDeleteA)
      # if hasPriv and adv:   optLayout.addWidget(lbtnSweepA)

      optLayout.addStretch()
      optFrame.setLayout(optLayout)

      self.frm = QtWidgets.QFrame()
      self.setWltDetailsFrame()

      totalFunds = self.wlt.getBalance('Total')
      spendFunds = self.wlt.getBalance('Spendable')
      unconfFunds = self.wlt.getBalance('Unconfirmed')
      uncolor = htmlColor('MoneyNeg')  if unconfFunds > 0          else htmlColor('Foreground')
      btccolor = htmlColor('DisableFG') if spendFunds == totalFunds else htmlColor('MoneyPos')
      lblcolor = htmlColor('DisableFG') if spendFunds == totalFunds else htmlColor('Foreground')
      goodColor = htmlColor('TextGreen')

      self.lblTot = QRichLabel('', doWrap=False)
      self.lblSpd = QRichLabel('', doWrap=False)
      self.lblUnc = QRichLabel('', doWrap=False)

      self.lblTotalFunds = QRichLabel('', doWrap=False)
      self.lblSpendFunds = QRichLabel('', doWrap=False)
      self.lblUnconfFunds = QRichLabel('', doWrap=False)
      self.lblTotalFunds.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
      self.lblSpendFunds.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
      self.lblUnconfFunds.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)

      self.lblTot.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
      self.lblSpd.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
      self.lblUnc.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)


      self.lblBTC1 = QRichLabel('', doWrap=False)
      self.lblBTC2 = QRichLabel('', doWrap=False)
      self.lblBTC3 = QRichLabel('', doWrap=False)

      ttipTot = createToolTipWidget(\
            self.tr('Total funds if all current transactions are confirmed. '
            'Value appears gray when it is the same as your spendable funds.'))
      ttipSpd = createToolTipWidget(\
            self.tr('Funds that can be spent <i>right now</i>'))
      ttipUcn = createToolTipWidget(\
            self.tr('Funds that have less than 6 confirmations'))

      self.setSummaryBalances()

      frmTotals = QtWidgets.QFrame()
      frmTotals.setFrameStyle(STYLE_NONE)
      frmTotalsLayout = QtWidgets.QGridLayout()
      frmTotalsLayout.addWidget(self.lblTot, 0, 0)
      frmTotalsLayout.addWidget(self.lblSpd, 1, 0)
      frmTotalsLayout.addWidget(self.lblUnc, 2, 0)

      frmTotalsLayout.addWidget(self.lblTotalFunds, 0, 1)
      frmTotalsLayout.addWidget(self.lblSpendFunds, 1, 1)
      frmTotalsLayout.addWidget(self.lblUnconfFunds, 2, 1)

      frmTotalsLayout.addWidget(self.lblBTC1, 0, 2)
      frmTotalsLayout.addWidget(self.lblBTC2, 1, 2)
      frmTotalsLayout.addWidget(self.lblBTC3, 2, 2)

      frmTotalsLayout.addWidget(ttipTot, 0, 3)
      frmTotalsLayout.addWidget(ttipSpd, 1, 3)
      frmTotalsLayout.addWidget(ttipUcn, 2, 3)

      frmTotals.setLayout(frmTotalsLayout)

      lblWltAddr = QRichLabel(self.tr('<b>Addresses in Wallet:</b>'), doWrap=False)

      btnGoBack = QtWidgets.QPushButton(self.tr('<<< Go Back'))
      btnGoBack.clicked.connect(self.accept)
      bottomFrm = makeHorizFrame([btnGoBack, STRETCH, frmTotals])

      layout = QtWidgets.QGridLayout()
      layout.addWidget(self.frm, 0, 0)
      layout.addWidget(self.wltAddrView, 2, 0)
      layout.addWidget(bottomFrm, 3, 0)

      # layout.addWidget(QtWidgets.QLabel("Available Actions:"), 0, 4)
      layout.addWidget(optFrame, 0, 1, 4, 1)
      layout.setRowStretch(0, 0)
      layout.setRowStretch(1, 0)
      layout.setRowStretch(2, 1)
      layout.setRowStretch(3, 0)
      layout.setColumnStretch(0, 1)
      layout.setColumnStretch(1, 0)
      self.setLayout(layout)
      self.setWindowTitle(self.tr('Wallet Properties'))

      #self.doFilterAddr()

      hexgeom = TheSettings.get('WltPropGeometry')
      tblgeom = TheSettings.get('WltPropAddrCols')

      if len(hexgeom) > 0:
         if type(hexgeom) == bytes:
            geom = hexgeom
         else:
            geom = QtCore.QByteArray(bytes.fromhex(hexgeom))
         self.restoreGeometry(geom)

      if len(tblgeom) > 0:
         restoreTableView(self.wltAddrView, tblgeom)

      def remindBackup():
         result = MsgBoxWithDNAA(self, self.main, MSGBOX.Warning, self.tr('Wallet Backup'), self.tr(
            '<b><font color="red" size=4>Please backup your wallet!</font></b> '
            '<br><br>'
            'Making a paper backup will guarantee you can recover your '
            'coins at <a>any time in the future</a>, even if your '
            'hard drive dies or you forget your passphrase.  Without it, '
            'you could permanently lose your coins!  '
            'The backup buttons are to the right of the address list.'
            '<br><br>'
            'A paper backup is recommended, '
            'and it can be copied by hand if you do not have a working printer. '
            'A digital backup only works if you remember the passphrase '
            'used at the time it was created.  If you have ever forgotten a '
            'password before, only rely on a digital backup if you store '
            'the password with it!'
            '<br><br>'
            '<a href="https://bitcointalk.org/index.php?topic=152151.0">'
            'Read more about Armory backups</a>'), None, yesStr='Ok', \
            dnaaStartChk=True)
         wlt.setSetting('DNAA_RemindBackup', result[1])

      wltType = determineWalletType(wlt, main)[0]
      chkLoad = (TheSettings.getSettingOrSetDefault('Load_Count', 1) % 5 == 0)
      chkType = not wltType in [WalletTypes.Offline, WalletTypes.WatchOnly]
      chkDNAA = not wlt.getSetting('DNAA_RemindBackup')
      chkDont = not TheSettings.getSettingOrSetDefault('DNAA_AllBackupWarn', False)
      if chkLoad and chkType and chkDNAA and chkDont:
         self.callLater(1, remindBackup)
         lbtnBackups.setText(self.tr('<font color="%s"><b>Backup This Wallet</b></font>' % htmlColor('TextWarn')))

   #############################################################################
   def doFilterAddr(self):
      self.wltAddrModel.setFilter(self.chkHideEmpty.isChecked(), \
                                  self.chkHideChange.isChecked(), \
                                  self.chkHideUnused.isChecked())
      self.wltAddrModel.reset()

   #############################################################################
   def setSummaryBalances(self):
      totalFunds = self.wlt.getBalance('Total')
      spendFunds = self.wlt.getBalance('Spendable')
      unconfFunds = self.wlt.getBalance('Unconfirmed')
      uncolor = htmlColor('MoneyNeg')  if unconfFunds > 0          else htmlColor('Foreground')
      btccolor = htmlColor('DisableFG') if spendFunds == totalFunds else htmlColor('MoneyPos')
      lblcolor = htmlColor('DisableFG') if spendFunds == totalFunds else htmlColor('Foreground')
      goodColor = htmlColor('TextGreen')

      self.lblTot.setText(self.tr('<b><font color="%s">Maximum Funds:</font></b>' % lblcolor))
      self.lblSpd.setText(self.tr('<b>Spendable Funds:</b>'))
      self.lblUnc.setText(self.tr('<b>Unconfirmed:</b>'))

      if TheBDM.getState() in (BDM_UNINITIALIZED, BDM_OFFLINE, BDM_SCANNING):
         totStr = '-' * 12
         spdStr = '-' * 12
         ucnStr = '-' * 12
      else:
         totStr = '<b><font color="%s">%s</font></b>' % (btccolor, coin2str(totalFunds))
         spdStr = '<b><font color="%s">%s</font></b>' % (goodColor, coin2str(spendFunds))
         ucnStr = '<b><font color="%s">%s</font></b>' % (uncolor, coin2str(unconfFunds))

      self.lblTotalFunds.setText(totStr)
      self.lblSpendFunds.setText(spdStr)
      self.lblUnconfFunds.setText(ucnStr)

      self.lblBTC1.setText('<b><font color="%s">BTC</font></b>' % lblcolor)
      self.lblBTC2.setText('<b>BTC</b>')
      self.lblBTC3.setText('<b>BTC</b>')

   #############################################################################
   def saveGeometrySettings(self):
      geom = self.saveGeometry().data().hex()
      TheSettings.set('WltPropGeometry', geom)
      TheSettings.set('WltPropAddrCols', saveTableView(self.wltAddrView))

   #############################################################################
   def closeEvent(self, event):
      self.saveGeometrySettings()
      super(DlgWalletDetails, self).closeEvent(event)

   #############################################################################
   def accept(self, *args):
      self.saveGeometrySettings()
      super(DlgWalletDetails, self).accept(*args)

   #############################################################################
   def reject(self, *args):
      self.saveGeometrySettings()
      super(DlgWalletDetails, self).reject(*args)

   #############################################################################
   def showContextMenu(self, pos):
      menu = QtWidgets.QMenu(self.wltAddrView)
      std = (self.main.usermode == USERMODE.Standard)
      adv = (self.main.usermode == USERMODE.Advanced)
      dev = (self.main.usermode == USERMODE.Expert)

      if True:  actionCopyAddr = menu.addAction(self.tr("Copy Address"))
      if True:  actionShowQRCode = menu.addAction(self.tr("Display Address QR Code"))
      if True:  actionBlkChnInfo = menu.addAction(self.tr("View Address on %s" % BLOCKEXPLORE_NAME))
      if True:  actionReqPayment = menu.addAction(self.tr("Request Payment to this Address"))
      if dev:   actionCopyHash160 = menu.addAction(self.tr("Copy Hash160 (hex)"))
      if dev:   actionCopyPubKey  = menu.addAction(self.tr("Copy Raw Public Key (hex)"))
      if True:  actionCopyComment = menu.addAction(self.tr("Copy Comment"))
      if True:  actionCopyBalance = menu.addAction(self.tr("Copy Balance"))
      try:
         idx = self.wltAddrView.selectedIndexes()[0]
      except IndexError:
         # Nothing was selected for a context menu to act upon.  Return.
         return
      action = menu.exec_(QtGui.QCursor.pos())

      # Get data on a given row, easily
      def getModelStr(col):
         model = self.wltAddrView.model()
         qstr = model.index(idx.row(), col).data().toString()
         return str(qstr).strip()

      addr = getModelStr(ADDRESSCOLS.Address)
      if action == actionCopyAddr:
         clippy = addr
      elif action == actionBlkChnInfo:
         blkchnURL = BLOCKEXPLORE_URL_ADDR % addr
         try:
            DlgBrowserWarn(blkchnURL).exec_()
         except:
            QtWidgets.QMessageBox.critical(self, self.tr('Could not open browser'), self.tr(
               'Armory encountered an error opening your web browser.  To view '
               'this address on blockchain.info, please copy and paste '
               'the following URL into your browser: '
               '<br><br>'
               '<a href="%s">%s</a>' % (blkchnURL, blkchnURL)), QtWidgets.QMessageBox.Ok)
         return
      elif action == actionShowQRCode:
         wltstr = 'Wallet: %s (%s)' % (self.wlt.labelName, self.wlt.walletId)
         DlgQRCodeDisplay(self, self.main, addr, addr, wltstr).exec_()
         return
      elif action == actionReqPayment:
         DlgRequestPayment(self, self.main, addr).exec_()
         return
      elif dev and action == actionCopyHash160:
         clippy = binary_to_hex(addrStr_to_hash160(addr)[1])
      elif dev and action == actionCopyPubKey:
         astr = getModelStr(ADDRESSCOLS.Address)
         addrObj = self.wlt.getAddrByHash160( addrStr_to_hash160(astr)[1] )
         clippy = addrObj.binPublicKey65.toHexStr()
      elif action == actionCopyComment:
         clippy = getModelStr(ADDRESSCOLS.Comment)
      elif action == actionCopyBalance:
         clippy = getModelStr(ADDRESSCOLS.Balance)
      else:
         return

      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(str(clippy).strip())

   #############################################################################
   def dblClickAddressView(self, index):
      from ui.TreeViewGUI import COL_TREE, COL_COMMENT
      nodeItem = self.wltAddrTreeModel.getNodeItem(index)
      try:
         if not nodeItem.treeNode.canDoubleClick():
            return
      except:
         return

      addrObj = nodeItem.treeNode.getAddrObj()
      if index.column() == COL_COMMENT:
         # Update the address's comment. We apparently need to reset the model
         # to get an immediate comment update on OS X, unlike Linux or Windows.
         currComment = addrObj.getComment()
         if not currComment:
            dialog = DlgSetComment(self, self.main, currComment, self.tr('Add Address Comment'))
         else:
            dialog = DlgSetComment(self, self.main, currComment, self.tr('Change Address Comment'))
         if dialog.exec_():
            newComment = str(dialog.edtComment.text())
            addr160 = addrObj.getAddr160()
            self.wlt.setComment(addr160, newComment)
      else:
         try:
            dlg = DlgAddressInfo(self.wlt, addrObj, self, self.main)
            dlg.exec_()
         except Exception as e:
            MsgBoxCustom(MSGBOX.Error, title="Error", msg=str(e))
            return

   #############################################################################
   def changeLabels(self):
      dlgLabels = DlgChangeLabels(self.wlt.labelName, self.wlt.labelDescr, self, self.main)
      if dlgLabels.exec_():
         # Make sure to use methods like this which not only update in memory,
         # but guarantees the file is updated, too
         newName = str(dlgLabels.edtName.text())[:32]
         newDescr = str(dlgLabels.edtDescr.toPlainText())[:256]
         self.wlt.setLabels(newName, newDescr)

         self.labelValues[WLTFIELDS.Name].setText(newName)
         self.labelValues[WLTFIELDS.Descr].setText(newDescr)

   #############################################################################
   def changeEncryption(self):
      dlgCrypt = DlgChangePassphrase(self, self.main, not self.wlt.useEncryption)
      if dlgCrypt.exec_():
         self.disableEncryption = dlgCrypt.chkDisableCrypt.isChecked()
         newPassphrase = SecureBinaryData(str(dlgCrypt.edtPasswd1.text()))
         dlgCrypt.edtPasswd1.clear()
         dlgCrypt.edtPasswd2.clear()

         if self.wlt.useEncryption:
            origPassphrase = SecureBinaryData(str(dlgCrypt.edtPasswdOrig.text()))
            dlgCrypt.edtPasswdOrig.clear()
            if self.wlt.verifyPassphrase(origPassphrase):
               unlockProgress = DlgProgress(self, self.main, HBar=1,
                                            Title="Unlocking Wallet")
               unlockProgress.exec_(self.wlt.unlock, securePassphrase=origPassphrase)
            else:
               # Even if the wallet is already unlocked, enter pwd again to change it
               QtWidgets.QMessageBox.critical(self, self.tr('Invalid Passphrase'), \
                     self.tr('Previous passphrase is not correct!  Could not unlock wallet.'), \
                     QtWidgets.QMessageBox.Ok)


         if self.disableEncryption:
            unlockProgress = DlgProgress(self, self.main, HBar=1,
                                         Title=self.tr("Changing Encryption"))
            unlockProgress.exec_(self.wlt.changeWalletEncryption)
            # self.accept()
            self.labelValues[WLTFIELDS.Secure].setText(self.tr('No Encryption'))
            self.labelValues[WLTFIELDS.Secure].setText('')
            self.labelValues[WLTFIELDS.Secure].setText('')
         else:
            if not self.wlt.useEncryption:
               kdfParams = self.wlt.computeSystemSpecificKdfParams(0.2)
               self.wlt.changeKdfParams(*kdfParams)
            unlockProgress = DlgProgress(self, self.main, HBar=2,
                                         Title=self.tr("Changing Encryption"))
            unlockProgress.exec_(self.wlt.changeWalletEncryption,
                                 securePassphrase=newPassphrase)
            self.labelValues[WLTFIELDS.Secure].setText(self.tr('Encrypted (AES256)'))
            # self.accept()

   def getNewAddress(self):
      if ShowRecvCoinsWarningIfNecessary(self.wlt, self, self.main):
         loading = LoadingDisp(self, self.main)
         loading.show()
         DlgNewAddressDisp(self.wlt, self, self.main, loading).exec_()
         self.resetTreeView()

   def execSendBtc(self):
      if TheBDM.getState() in (BDM_OFFLINE, BDM_UNINITIALIZED):
         QtWidgets.QMessageBox.warning(self, self.tr('Offline Mode'), self.tr(
           'Armory is currently running in offline mode, and has no '
           'ability to determine balances or create transactions. '
           '<br><br> '
           'In order to send coins from this wallet you must use a '
           'full copy of this wallet from an online computer, '
           'or initiate an "offline transaction" using a watching-only '
           'wallet on an online computer.'), QtWidgets.QMessageBox.Ok)
         return
      if TheBDM.getState() == BDM_SCANNING:
         QtWidgets.QMessageBox.warning(self, self.tr('Armory Not Ready'), self.tr(
           'Armory is currently scanning the blockchain to collect '
           'the information needed to create transactions.  This '
           'typically takes between one and five minutes.  Please '
           'wait until your balance appears on the main window, '
           'then try again.'), \
            QtWidgets.QMessageBox.Ok)
         return

      self.accept()
      DlgSendBitcoins(self.wlt, self, self.main, onlyOfflineWallets=False).exec_()
      self.resetTreeView()

   def resetTreeView(self):
      self.wltAddrTreeModel.refresh()
      self.wltAddrView.reset()

   def changeKdf(self):
      """
      This is a low-priority feature.  I mean, the PyBtcWallet class has this
      feature implemented, but I don't have a GUI for it
      """
      pass

   def execBackupDlg(self):
      if self.main.usermode == USERMODE.Expert:
         DlgBackupCenter(self, self.main, self.wlt).exec_()
      else:
         DlgSimpleBackup(self, self.main, self.wlt).exec_()

   def execPrintDlg(self):
      if self.wlt.isLocked:
         unlockdlg = DlgUnlockWallet(self.wlt, self, self.main, self.tr('Create Paper Backup'))
         if not unlockdlg.exec_():
            return

      if not self.wlt.addrMap['ROOT'].hasPrivKey():
         QtWidgets.QMessageBox.warning(self, self.tr('Move along...'), \
           self.tr('This wallet does not contain any private keys.  Nothing to backup!'), QtWidgets.QMessageBox.Ok)
         return

      OpenPaperBackupDialog('Single', self, self.main, self.wlt)

   def execRemoveDlg(self):
      dlg = DlgRemoveWallet(self.wlt, self, self.main)
      if dlg.exec_():
         pass  # not sure that I don't handle everything in the dialog itself

   def execKeyList(self):
      if self.wlt.useEncryption and self.wlt.isLocked:
         dlg = DlgUnlockWallet(self.wlt, self, self.main, self.tr('Unlock Private Keys'))
         if not dlg.exec_():
            if self.main.usermode == USERMODE.Expert:
               QtWidgets.QMessageBox.warning(self, self.tr('Unlock Failed'), self.tr(
                  'Wallet was not unlocked.  The public keys and addresses '
                  'will still be shown, but private keys will not be available '
                  'unless you reopen the dialog with the correct passphrase'), \
                  QtWidgets.QMessageBox.Ok)
            else:
               QtWidgets.QMessageBox.warning(self, self.tr('Unlock Failed'), self.tr(
                  'Wallet could not be unlocked to display individual keys.'), \
                  QtWidgets.QMessageBox.Ok)
               return

      dlg = DlgShowKeyList(self.wlt, self, self.main)
      dlg.exec_()

   def execDeleteAddress(self):
      selectedList = self.wltAddrView.selectedIndexes()
      if len(selectedList) == 0:
         QtWidgets.QMessageBox.warning(self, self.tr('No Selection'), \
               self.tr('You must select an address to remove!'), \
               QtWidgets.QMessageBox.Ok)
         return

      nodeIndex = selectedList[0]
      nodeItem = self.wltAddrTreeModel.getNodeItem(nodeIndex)
      addrStr = nodeItem.treeNode.getName()
      atype, addr160 = addrStr_to_hash160(addrStr)
      if atype==P2SHBYTE:
         LOGWARN('Deleting P2SH address: %s' % addrStr)


      if self.wlt.cppWallet.getAssetIndexForAddr(addr160) < 0:
         dlg = DlgRemoveAddress(self.wlt, addr160, self, self.main)
         dlg.exec_()
      else:
         QtWidgets.QMessageBox.warning(self, self.tr('Invalid Selection'), self.tr(
               'You cannot delete addresses generated by your wallet. '
               'Only imported addresses can be deleted.'), \
               QtWidgets.QMessageBox.Ok)
         return

   def execImportAddress(self):
      if not TheSettings.getSettingOrSetDefault('DNAA_ImportWarning', False):
         result = MsgBoxWithDNAA(self, self.main, MSGBOX.Warning, \
            self.tr('Imported Address Warning'), self.tr(
            'Armory supports importing of external private keys into your '
            'wallet but imported addresses are <u>not</u> automatically '
            'protected by your backups.  If you do not plan to use the '
            'address again, it is recommended that you "Sweep" the private '
            'key instead of importing it. '
            '<br><br> '
            'Individual private keys, including imported ones, can be '
            'backed up using the "Export Key Lists" option in the wallet '
            'backup window.'), None)
         TheSettings.set('DNAA_ImportWarning', result[1])

      # Now we are past the [potential] warning box.  Actually open
      # the import dialog
      dlg = DlgImportAddress(self.wlt, self, self.main)
      dlg.exec_()

      try:
         self.parent.wltAddrModel.reset()
      except AttributeError:
         pass

   #############################################################################
   def execExpWOCopy(self):
      """
      Function executed when a user executes the \"Export Public Key & Chain
      Code\" option.
      """
      # This should never happen....
      if not self.wlt.addrMap['ROOT'].hasChainCode():
         QtWidgets.QMessageBox.warning(self, self.tr(
            'Move along... This wallet does not have '
            'a chain code. Backups are pointless!'),
            QtWidgets.QMessageBox.Ok)
         return

      # Proceed to the actual export center.
      dlg = DlgExpWOWltData(self.wlt, self, self.main)
      if dlg.exec_():
         pass  # Once executed, we're done.

   #############################################################################
   def setWltDetailsFrame(self):
      dispCrypto = self.wlt.useEncryption and \
         self.usermode in [USERMODE.Advanced, USERMODE.Expert]
      self.wltID = self.wlt.walletId

      if dispCrypto:
         mem = self.wlt.getKdfMemoryReqtBytes()
         kdfmemstr = str(mem / 1024) + ' kB'
         if mem >= 1024 * 1024:
            kdfmemstr = str(mem / (1024 * 1024)) + ' MB'

      tooltips = [[]] * 10
      tooltips[WLTFIELDS.Name] = createToolTipWidget(self.tr(
            'This is the name stored with the wallet file.  Click on the '
            '"Change Labels" button on the right side of this '
            'window to change this field'))

      tooltips[WLTFIELDS.Descr] = createToolTipWidget(self.tr(
            'This is the description of the wallet stored in the wallet file. '
            'Press the "Change Labels" button on the right side of this '
            'window to change this field'))

      tooltips[WLTFIELDS.WltID] = createToolTipWidget(self.tr(
            'This is a unique identifier for this wallet, based on the root key. '
            'No other wallet can have the same ID '
            'unless it is a copy of this one, regardless of whether '
            'the name and description match.'))

      tooltips[WLTFIELDS.NumAddr] = createToolTipWidget(self.tr(
            'This is the number of addresses *used* by this wallet so far. '
            'If you recently restored this wallet and you do not see all the '
            'funds you were expecting, click on this field to increase it.'))

      if self.typestr == 'Offline':
         tooltips[WLTFIELDS.Secure] = createToolTipWidget(self.tr(
            'Offline:  This is a "Watching-Only" wallet that you have identified '
            'belongs to you, but you cannot spend any of the wallet funds '
            'using this wallet.  This kind of wallet '
            'is usually stored on an internet-connected computer, to manage '
            'incoming transactions, but the private keys needed '
            'to spend the money are stored on an offline computer.'))
      elif self.typestr == 'Watching-Only':
         tooltips[WLTFIELDS.Secure] = createToolTipWidget(self.tr(
            'Watching-Only:  You can only watch addresses in this wallet '
            'but cannot spend any of the funds.'))
      elif self.typestr == 'No Encryption':
         tooltips[WLTFIELDS.Secure] = createToolTipWidget(self.tr(
            'No Encryption: This wallet contains private keys, and does not require '
            'a passphrase to spend funds available to this wallet.  If someone '
            'else obtains a copy of this wallet, they can also spend your funds! '
            '(You can click the "Change Encryption" button on the right side of this '
            'window to enabled encryption)'))
      elif self.typestr == 'Encrypted (AES256)':
         tooltips[WLTFIELDS.Secure] = createToolTipWidget(self.tr(
            'This wallet contains the private keys needed to spend this wallet\'s '
            'funds, but they are encrypted on your harddrive.  The wallet must be '
            '"unlocked" with the correct passphrase before you can spend any of the '
            'funds.  You can still generate new addresses and monitor incoming '
            'transactions, even with a locked wallet.'))

      tooltips[WLTFIELDS.BelongsTo] = createToolTipWidget(self.tr(
            'Declare who owns this wallet.  If you click on the field and select '
            '"This wallet is mine", it\'s balance will be included in your total '
            'Armory Balance in the main window'))

      tooltips[WLTFIELDS.Time] = createToolTipWidget(self.tr(
            'This is exactly how long it takes your computer to unlock your '
            'wallet after you have entered your passphrase.  If someone got '
            'ahold of your wallet, this is approximately how long it would take '
            'them to for each guess of your passphrase.'))

      tooltips[WLTFIELDS.Mem] = createToolTipWidget(self.tr(
            'This is the amount of memory required to unlock your wallet. '
            'Memory values above 64 kB pretty much guarantee that GPU-acceleration '
            'will be useless for guessing your passphrase'))

      tooltips[WLTFIELDS.Version] = createToolTipWidget(self.tr(
            'Wallets created with different versions of Armory, may have '
            'different wallet versions.  Not all functionality may be '
            'available with all wallet versions.  Creating a new wallet will '
            'always create the latest version.'))
      labelNames = [[]] * 10
      labelNames[WLTFIELDS.Name] = QtWidgets.QLabel(self.tr('Wallet Name:'))
      labelNames[WLTFIELDS.Descr] = QtWidgets.QLabel(self.tr('Description:'))

      labelNames[WLTFIELDS.WltID] = QtWidgets.QLabel(self.tr('Wallet ID:'))
      labelNames[WLTFIELDS.NumAddr] = QtWidgets.QLabel(self.tr('Addresses Used:'))
      labelNames[WLTFIELDS.Secure] = QtWidgets.QLabel(self.tr('Security:'))
      labelNames[WLTFIELDS.Version] = QtWidgets.QLabel(self.tr('Version:'))

      labelNames[WLTFIELDS.BelongsTo] = QtWidgets.QLabel(self.tr('Belongs to:'))


      # TODO:  Add wallet path/location to this!

      if dispCrypto:
         labelNames[WLTFIELDS.Time] = QtWidgets.QLabel(self.tr('Unlock Time:'))
         labelNames[WLTFIELDS.Mem] = QtWidgets.QLabel(self.tr('Unlock Memory:'))

      self.labelValues = [[]] * 10
      self.labelValues[WLTFIELDS.Name] = QtWidgets.QLabel(self.wlt.labelName)
      self.labelValues[WLTFIELDS.Descr] = QtWidgets.QLabel(self.wlt.labelDescr)

      self.labelValues[WLTFIELDS.WltID] = QtWidgets.QLabel(self.wlt.walletId)
      self.labelValues[WLTFIELDS.Secure] = QtWidgets.QLabel(self.typestr)
      self.labelValues[WLTFIELDS.BelongsTo] = QtWidgets.QLabel('')
      self.labelValues[WLTFIELDS.Version] = QtWidgets.QLabel(getVersionString(self.wlt.version))


      topUsed = max(self.wlt.highestUsedChainIndex, 0)
      self.labelValues[WLTFIELDS.NumAddr] = QLabelButton('%d' % topUsed)
      self.labelValues[WLTFIELDS.NumAddr].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
      opendlgkeypool = lambda: DlgKeypoolSettings(self.wlt, self, self.main).exec_()
      self.labelValues[WLTFIELDS.NumAddr].linkActivated.connect(opendlgkeypool)

      # Set the owner appropriately
      if self.wlt.watchingOnly:
         if self.wlt.getSetting('IsMine'):
            self.labelValues[WLTFIELDS.BelongsTo] = QLabelButton(self.tr('You own this wallet'))
            self.labelValues[WLTFIELDS.BelongsTo].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         else:
            owner = self.wlt.getSetting('BelongsTo')
            if owner == '':
               self.labelValues[WLTFIELDS.BelongsTo] = QLabelButton(self.tr('Someone else...'))
               self.labelValues[WLTFIELDS.BelongsTo].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            else:
               self.labelValues[WLTFIELDS.BelongsTo] = QLabelButton(owner)
               self.labelValues[WLTFIELDS.BelongsTo].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)

         self.labelValues[WLTFIELDS.BelongsTo].linkActivated.connect(self.execSetOwner)

      if dispCrypto:
         self.labelValues[WLTFIELDS.Time] = QLabelButton(self.tr('Click to Test'))
         self.labelValues[WLTFIELDS.Mem] = QtWidgets.QLabel(kdfmemstr)

      for ttip in tooltips:
         try:
            ttip.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignTop)
            w, h = relaxedSizeStr(ttip, '(?)')
            ttip.setMaximumSize(w, h)
         except AttributeError:
            pass

      for lbl in labelNames:
         try:
            lbl.setTextFormat(QtCore.Qt.RichText)
            lbl.setText('<b>' + lbl.text() + '</b>')
            lbl.setContentsMargins(0, 0, 0, 0)
            w, h = tightSizeStr(lbl, '9' * 16)
            lbl.setMaximumSize(w, h)
         except AttributeError:
            pass


      for i, lbl in enumerate(self.labelValues):
         if i == WLTFIELDS.BelongsTo:
            lbl.setContentsMargins(10, 0, 10, 0)
            continue
         try:
            lbl.setText('<i>' + lbl.text() + '</i>')
            lbl.setContentsMargins(10, 0, 10, 0)
            # lbl.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | \
                                        # QtCore.Qt.TextSelectableByKeyboard)
         except AttributeError:
            pass

      # Not sure why this has to be connected downhere... it didn't work above it
      if dispCrypto:
         self.labelValues[WLTFIELDS.Time].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         self.labelValues[WLTFIELDS.Time].linkActivated.connect(self.testKdfTime)

      labelNames[WLTFIELDS.Descr].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)
      self.labelValues[WLTFIELDS.Descr].setWordWrap(True)
      self.labelValues[WLTFIELDS.Descr].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)

      lblEmpty = QtWidgets.QLabel(' ' * 20)

      layout = QtWidgets.QGridLayout()

      layout.addWidget(tooltips[WLTFIELDS.WltID], 0, 0);
      layout.addWidget(labelNames[WLTFIELDS.WltID], 0, 1);
      layout.addWidget(self.labelValues[WLTFIELDS.WltID], 0, 2)

      layout.addWidget(tooltips[WLTFIELDS.Name], 1, 0);
      layout.addWidget(labelNames[WLTFIELDS.Name], 1, 1);
      layout.addWidget(self.labelValues[WLTFIELDS.Name], 1, 2)

      layout.addWidget(tooltips[WLTFIELDS.Descr], 2, 0);
      layout.addWidget(labelNames[WLTFIELDS.Descr], 2, 1);
      layout.addWidget(self.labelValues[WLTFIELDS.Descr], 2, 2, 4, 1)

      layout.addWidget(tooltips[WLTFIELDS.Version], 0, 3);
      layout.addWidget(labelNames[WLTFIELDS.Version], 0, 4);
      layout.addWidget(self.labelValues[WLTFIELDS.Version], 0, 5)

      i = 0
      if self.main.usermode == USERMODE.Expert:
         i += 1
         layout.addWidget(tooltips[WLTFIELDS.NumAddr], i, 3)
         layout.addWidget(labelNames[WLTFIELDS.NumAddr], i, 4)
         layout.addWidget(self.labelValues[WLTFIELDS.NumAddr], i, 5)

      i += 1
      layout.addWidget(tooltips[WLTFIELDS.Secure], i, 3);
      layout.addWidget(labelNames[WLTFIELDS.Secure], i, 4);
      layout.addWidget(self.labelValues[WLTFIELDS.Secure], i, 5)


      if self.wlt.watchingOnly:
         i += 1
         layout.addWidget(tooltips[WLTFIELDS.BelongsTo], i, 3);
         layout.addWidget(labelNames[WLTFIELDS.BelongsTo], i, 4);
         layout.addWidget(self.labelValues[WLTFIELDS.BelongsTo], i, 5)


      if dispCrypto:
         i += 1
         layout.addWidget(tooltips[WLTFIELDS.Time], i, 3);
         layout.addWidget(labelNames[WLTFIELDS.Time], i, 4);
         layout.addWidget(self.labelValues[WLTFIELDS.Time], i, 5)

         i += 1
         layout.addWidget(tooltips[WLTFIELDS.Mem], i, 3);
         layout.addWidget(labelNames[WLTFIELDS.Mem], i, 4);
         layout.addWidget(self.labelValues[WLTFIELDS.Mem], i, 5)


      self.frm = QtWidgets.QFrame()
      self.frm.setFrameStyle(STYLE_SUNKEN)
      self.frm.setLayout(layout)

   def testKdfTime(self):
      kdftimestr = "%0.3f sec" % self.wlt.testKdfComputeTime()
      self.labelValues[WLTFIELDS.Time].setText(kdftimestr)

   def execSetOwner(self):
      dlg = self.dlgChangeOwner(self.wlt, self, self.main)
      if dlg.exec_():
         if dlg.chkIsMine.isChecked():
            self.wlt.setSetting('IsMine', True)
            self.wlt.setSetting('BelongsTo', '')
            self.labelValues[WLTFIELDS.BelongsTo].setText(self.tr('You own this wallet'))
            self.labelValues[WLTFIELDS.BelongsTo].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            self.labelValues[WLTFIELDS.Secure].setText(self.tr('<i>Offline</i>'))
         else:
            owner = unicode(dlg.edtOwnerString.text())
            self.wlt.setSetting('IsMine', False)
            self.wlt.setSetting('BelongsTo', owner)

            if len(owner) > 0:
               self.labelValues[WLTFIELDS.BelongsTo].setText(owner)
            else:
               self.labelValues[WLTFIELDS.BelongsTo].setText(self.tr('Someone else'))
            self.labelValues[WLTFIELDS.Secure].setText(self.tr('<i>Watching-Only</i>'))
            self.labelValues[WLTFIELDS.BelongsTo].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            self.labelValues[WLTFIELDS.Secure].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)

         self.main.changeWltFilter()

   #############################################################################
   class dlgChangeOwner(ArmoryDialog):
      def __init__(self, wlt, parent=None, main=None):
         super(parent.dlgChangeOwner, self).__init__(parent, main)

         layout = QtWidgets.QGridLayout()
         self.chkIsMine = QtWidgets.QCheckBox(self.tr('This wallet is mine'))
         self.edtOwnerString = QtWidgets.QLineEdit()
         if wlt.getSetting('IsMine'):
            lblDescr = QtWidgets.QLabel(self.tr(
               'The funds in this wallet are currently identified as '
               'belonging to <b><i>you</i></b>.  As such, any funds '
               'available to this wallet will be included in the total '
               'balance displayed on the main screen.  \n\n '
               'If you do not actually own this wallet, or do not wish '
               'for its funds to be considered part of your balance, '
               'uncheck the box below.  Optionally, you can include the '
               'name of the person or organization that does own it.'))
            lblDescr.setWordWrap(True)
            layout.addWidget(lblDescr, 0, 0, 1, 2)
            layout.addWidget(self.chkIsMine, 1, 0)
            self.chkIsMine.setChecked(True)
            self.edtOwnerString.setEnabled(False)
         else:
            owner = wlt.getSetting('BelongsTo')
            if owner == '':
               owner = 'someone else'
            else:
               self.edtOwnerString.setText(owner)
            lblDescr = QtWidgets.QLabel(self.tr(
               'The funds in this wallet are currently identified as '
               'belonging to <i><b>%s</b></i>.  If these funds are actually '
               'yours, and you would like the funds included in your balance in '
               'the main window, please check the box below.\n\n' % owner))
            lblDescr.setWordWrap(True)
            layout.addWidget(lblDescr, 0, 0, 1, 2)
            layout.addWidget(self.chkIsMine, 1, 0)

            ttip = createToolTipWidget(self.tr(
               'You might choose this option if you keep a full '
               'wallet on a non-internet-connected computer, and use this '
               'watching-only wallet on this computer to generate addresses '
               'and monitor incoming transactions.'))
            layout.addWidget(ttip, 1, 1)


         slot = lambda b: self.edtOwnerString.setEnabled(not b)
         self.chkIsMine.toggled.connect(slot)

         layout.addWidget(QtWidgets.QLabel(self.tr('Wallet owner (optional):')), 3, 0)
         layout.addWidget(self.edtOwnerString, 3, 1)
         bbox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
            QtWidgets.QDialogButtonBox.Cancel)
         bbox.accepted.connect(self.accept)
         bbox.rejected.connect(self.reject)
         layout.addWidget(bbox, 4, 0)
         self.setLayout(layout)
         self.setWindowTitle(self.tr('Set Wallet Owner'))

################################################################################
class DlgChangeLabels(ArmoryDialog):
   def __init__(self, currName='', currDescr='', parent=None, main=None):
      super(DlgChangeLabels, self).__init__(parent, main)

      self.edtName = QtWidgets.QLineEdit()
      self.edtName.setMaxLength(32)
      lblName = QtWidgets.QLabel(self.tr("Wallet &name:"))
      lblName.setBuddy(self.edtName)

      self.edtDescr = QtWidgets.QTextEdit()
      tightHeight = tightSizeNChar(self.edtDescr, 1)[1]
      self.edtDescr.setMaximumHeight(int(tightHeight * 4.2))
      lblDescr = QtWidgets.QLabel(self.tr("Wallet &description:"))
      lblDescr.setAlignment(QtCore.Qt.AlignVCenter)
      lblDescr.setBuddy(self.edtDescr)

      self.edtName.setText(currName)
      self.edtDescr.setText(currDescr)

      buttonBox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
                                   QtWidgets.QDialogButtonBox.Cancel)
      buttonBox.accepted.connect(self.accept)
      buttonBox.rejected.connect(self.reject)

      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblName, 1, 0, 1, 1)
      layout.addWidget(self.edtName, 1, 1, 1, 1)
      layout.addWidget(lblDescr, 2, 0, 1, 1)
      layout.addWidget(self.edtDescr, 2, 1, 2, 1)
      layout.addWidget(buttonBox, 4, 0, 1, 2)
      self.setLayout(layout)

      self.setWindowTitle(self.tr('Wallet Descriptions'))


   def accept(self, *args):
      try:
         self.edtName.text().encode("ascii")
      except UnicodeDecodeError:
         UnicodeErrorBox(self)
         return

      if len(str(self.edtName.text()).strip()) == 0:
         QtWidgets.QMessageBox.critical(self, self.tr('Empty Name'), \
            self.tr('All wallets must have a name. '), QtWidgets.QMessageBox.Ok)
         return
      super(DlgChangeLabels, self).accept(*args)
