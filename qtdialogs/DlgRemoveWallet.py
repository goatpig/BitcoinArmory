################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2025, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

from armoryengine.BDM import TheBDM, BDM_BLOCKCHAIN_READY
from armoryengine.ArmoryUtils import LOGINFO, LOGERROR, coin2str

from qtpy import QtCore, QtGui, QtWidgets
from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgBackupCenter import OpenPaperBackupDialog
from qtdialogs.qtdefines import QRichLabel, makeLayoutFrame, USERMODE, \
   createToolTipWidget, STYLE_RAISED, HORIZONTAL

################################################################################
class DlgRemoveWallet(ArmoryDialog):
   def __init__(self, wlt, parent=None, main=None):
      super().__init__(parent, main)

      wltID = wlt.walletId
      wltName = wlt.labelName
      wltDescr = wlt.labelDescr
      lblWarning = QtWidgets.QLabel(self.tr('<b>!!! WARNING !!!</b>\n\n'))
      lblWarning.setTextFormat(QtCore.Qt.RichText)
      lblWarning.setAlignment(QtCore.Qt.AlignHCenter)

      lblWarning2 = QtWidgets.QLabel(self.tr(
         '<i>You have requested that the following wallet '
         'be removed from Armory:</i>'))
      lblWarning.setTextFormat(QtCore.Qt.RichText)
      lblWarning.setWordWrap(True)
      lblWarning.setAlignment(QtCore.Qt.AlignHCenter)

      lbls = []
      lbls.append([])
      lbls[0].append(QtWidgets.QLabel(self.tr('Wallet Unique ID:')))
      lbls[0].append(QtWidgets.QLabel(wltID))
      lbls.append([])
      lbls[1].append(QtWidgets.QLabel(self.tr('Wallet Name:')))
      lbls[1].append(QtWidgets.QLabel(wlt.labelName))
      lbls.append([])
      lbls[2].append(QtWidgets.QLabel(self.tr('Description:')))
      lbls[2].append(QtWidgets.QLabel(wlt.labelDescr))
      lbls[2][-1].setWordWrap(True)

      wltEmpty = True
      if TheBDM.getState() == BDM_BLOCKCHAIN_READY:
         # Removed this line of code because it's part of the old BDM paradigm.
         # Leaving this comment here in case it needs to be replaced by anything
         bal = wlt.getBalance('Full')
         lbls.append([])
         lbls[3].append(QtWidgets.QLabel(self.tr(
            'Current Balance (w/ unconfirmed):')))
         if bal > 0:
            lbls[3].append(QtWidgets.QLabel(
               '<font color="red"><b>' +\
               coin2str(bal, maxZeros=1).strip() +\
               ' BTC</b></font>'))
            lbls[3][-1].setTextFormat(QtCore.Qt.RichText)
            wltEmpty = False
         else:
            lbls[3].append(QtWidgets.QLabel(coin2str(bal, maxZeros=1) + ' BTC'))

      # Add two WARNING images on either side of dialog
      lblWarnImg = QtWidgets.QLabel()
      lblWarnImg.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      lblWarnImg2 = QtWidgets.QLabel()
      lblWarnImg2.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg2.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      # Add the warning text and images to the top of the dialog
      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblWarning, 0, 1, 1, 1)
      layout.addWidget(lblWarning2, 1, 1, 1, 1)
      layout.addWidget(lblWarnImg, 0, 0, 2, 1)
      layout.addWidget(lblWarnImg2, 0, 2, 2, 1)

      frmInfo = QtWidgets.QFrame()
      frmInfo.setFrameStyle(QtWidgets.QFrame.Box | QtWidgets.QFrame.Plain)
      frmInfo.setLineWidth(3)
      frmInfoLayout = QtWidgets.QGridLayout()
      for i in range(len(lbls)):
         lbls[i][0].setText('<b>' + lbls[i][0].text() + '</b>')
         lbls[i][0].setTextFormat(QtCore.Qt.RichText)
         frmInfoLayout.addWidget(lbls[i][0], i, 0)
         frmInfoLayout.addWidget(lbls[i][1], i, 1, 1, 2)

      frmInfo.setLayout(frmInfoLayout)
      layout.addWidget(frmInfo, 2, 0, 2, 3)
      hasWarningRow = False
      if not wlt.watchingOnly:
         if not wltEmpty:
            lbl = QRichLabel(self.tr(
               '<b>WALLET IS NOT EMPTY.  Only delete this wallet if you '
               'have a backup on paper or saved to a another location '
               'outside your settings directory.</b>'))
            hasWarningRow = True
         elif wlt.isWltSigningAnyLockbox(self.main.allLockboxes):
            lbl = QRichLabel(self.tr(
               '<b>WALLET IS PART OF A LOCKBOX.  Only delete this wallet if you '
               'have a backup on paper or saved to a another location '
               'outside your settings directory.</b>'))
            hasWarningRow = True
         if hasWarningRow:
            lbls.append(lbl)
            layout.addWidget(lbl, 4, 0, 1, 3)

      self.radioDelete = QtWidgets.QRadioButton(self.tr(
         'Permanently delete this wallet'))
      self.radioWatch = QtWidgets.QRadioButton(self.tr(
         'Delete private keys only, make watching-only'))

      # Make sure that there can only be one selection
      btngrp = QtWidgets.QButtonGroup(self)
      btngrp.addButton(self.radioDelete)
      if not self.main.usermode == USERMODE.Standard:
         btngrp.addButton(self.radioWatch)
      btngrp.setExclusive(True)

      ttipDelete = createToolTipWidget(self.tr(
         'This will delete the wallet file, removing '
         'all its private keys from your settings directory. '
         'If you intend to keep using addresses from this '
         'wallet, do not select this option unless the wallet '
         'is backed up elsewhere.'))
      ttipWatch = createToolTipWidget(self.tr(
         'This will delete the private keys from your wallet, '
         'leaving you with a watching-only wallet, which can be '
         'used to generate addresses and monitor incoming '
         'payments.  This option would be used if you created '
         'the wallet on this computer <i>in order to transfer '
         'it to a different computer or device and want to '
         'remove the private data from this system for security.</i>'))

      self.chkPrintBackup = QtWidgets.QCheckBox(self.tr(
         'Print a paper backup of this wallet before deleting'))

      if wlt.watchingOnly:
         ttipDelete = createToolTipWidget(self.tr(
            'This will delete the wallet file from your system. '
            'Since this is a watching-only wallet, no private keys '
            'will be deleted.'))
         ttipWatch = createToolTipWidget(self.tr(
            'This wallet is already a watching-only wallet so this option '
            'is pointless'))
         self.radioWatch.setEnabled(False)
         self.chkPrintBackup.setEnabled(False)

      self.frm = []
      rdoFrm = QtWidgets.QFrame()
      rdoFrm.setFrameStyle(STYLE_RAISED)
      rdoLayout = QtWidgets.QGridLayout()

      startRow = 0
      for rdo, ttip in [
         (self.radioDelete, ttipDelete),
         (self.radioWatch, ttipWatch)]:
         self.frm.append(QtWidgets.QFrame())
         # self.frm[-1].setFrameStyle(STYLE_SUNKEN)
         self.frm[-1].setFrameStyle(QtWidgets.QFrame.NoFrame)
         frmLayout = QtWidgets.QHBoxLayout()
         frmLayout.addWidget(rdo)
         ttip.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         frmLayout.addWidget(ttip)
         frmLayout.addStretch()
         self.frm[-1].setLayout(frmLayout)
         rdoLayout.addWidget(self.frm[-1], startRow, 0, 1, 3)
         startRow += 1

      self.radioDelete.setChecked(True)
      rdoFrm.setLayout(rdoLayout)
      startRow = 6 if not hasWarningRow else 5
      layout.addWidget(rdoFrm, startRow, 0, 1, 3)

      if wlt.watchingOnly:
         self.frm[-1].setVisible(False)

      printTtip = createToolTipWidget(self.tr(
         'If this box is checked, you will have the ability to print off an '
         'unencrypted version of your wallet before it is deleted.  <b>If '
         'printing is unsuccessful, please press *CANCEL* on the print dialog '
         'to prevent the delete operation from continuing</b>'))
      printFrm = makeLayoutFrame(HORIZONTAL, [
         self.chkPrintBackup, printTtip, 'Stretch'])
      startRow += 1
      layout.addWidget(printFrm, startRow, 0, 1, 3)

      if wlt.watchingOnly:
         printFrm.setVisible(False)

      startRow += 1
      self.btnDelete = QtWidgets.QPushButton(self.tr("Delete"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnDelete.clicked.connect(lambda: self.removeWallet(wlt))
      self.btnCancel.clicked.connect(self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnDelete, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      layout.addWidget(buttonBox, startRow, 0, 1, 3)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Delete Wallet Options'))

   ########
   def removeWallet(self, wlt):
      # Open the print dialog. If they hit cancel at any time, then
      # we go back to the primary wallet-remove dialog without any other action
      if self.chkPrintBackup.isChecked():
         result = OpenPaperBackupDialog('Single', self, self.main, wlt)
         if not result:
            QtWidgets.QMessageBox.warning(self, self.tr('Operation Aborted'),
               self.tr('You requested a paper backup before deleting the '
                  'wallet, but clicked "Cancel" on the backup printing window. '
                  'So, the delete operation was canceled as well.'),
               QtWidgets.QMessageBox.Ok)
            return

      # If they only want to exclude the wallet, we will add it to the excluded
      # list and remove it from the application. The wallet files will remain
      # in the settings directory but will be ignored by Armory
      if wlt.watchingOnly:
         reply = QtWidgets.QMessageBox.warning(self, self.tr('Confirm Delete'),
            self.tr('You are about to delete a watching-only wallet. Are you '
               'sure you want to do this?'),
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)

      elif self.radioDelete.isChecked():
         reply = QtWidgets.QMessageBox.warning(self,
            self.tr('Are you absolutely sure?!?'),
            self.tr('Are you absolutely sure you want to permanently delete '
               'this wallet?  Unless this wallet is saved on another device '
               'you will permanently lose access to all the addresses in this '
               'wallet.'),
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)

      elif self.radioWatch.isChecked():
         reply = QtWidgets.QMessageBox.warning(self,
            self.tr('Are you absolutely sure?!?'),
            self.tr('<i>This will permanently delete the information you need '
               'to spend funds from this wallet!</i>  You will only be able to '
               'receive coins, but not spend them.  Only do this if you have '
               'another copy of this wallet elsewhere, such as a paper backup '
               'or on an offline computer with the full wallet.'),
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)

      if reply == QtWidgets.QMessageBox.Yes:
         if self.radioWatch.isChecked():
            LOGINFO('***Converting to watching-only wallet')
            newWltPath = wlt.getWalletPath('WatchOnly')
            wlt.forkOnlineWallet(newWltPath, wlt.labelName, wlt.labelDescr)
            self.main.removeWalletFromApplication(wlt.walletId)

            newWlt = PyBtcWallet().readWalletFile(newWltPath)
            self.main.addWalletToApplication(newWlt, True)
            os.remove(thepath)
            os.remove(thepathBackup)
            self.main.statusBar().showMessage(self.tr(
               f'Wallet {wlt.walletId} was replaced with a watching-only wallet.'),
               10000)

         elif self.radioDelete.isChecked():
            LOGINFO('***Completely deleting wallet')
            if not wlt.delete():
               LOGERROR("failed to delete wallet")
               raise Exception("failed to delete wallet")

            self.main.removeWalletFromApplication(wlt.walletId)
            self.main.statusBar().showMessage(
               self.tr('Wallet %s was deleted!' % wlt.walletId), 10000)

         self.accept()
         self.parent.accept()
      else:
         self.reject()
