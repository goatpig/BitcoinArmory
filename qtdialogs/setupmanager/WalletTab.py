################################################################################
#                                                                              #
#  Copyright (C) 2026, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

import os

from qtpy import QtCore, QtWidgets
from armoryengine.ArmoryUtils import ARMORY_HOME_DIR, LOGINFO, LOGERROR
from armoryengine.WalletUtils import WalletList
from armoryengine.CppBridge import TheBridge
from ui.QtExecuteSignal import TheSignalExecution

from qtdialogs.DlgWalletMigration import DlgWalletMigration
from qtdialogs.DlgUnlockWallet import UnlockWalletHandler
import qtdialogs.qtdefines as qtdefines

################################################################################
class WalletTab(QtWidgets.QWidget):
   """
   Wallet settings tab for Setup Manager.

   Manages:
   - Armory data directory selection
   - Wallet list display with migrate/unlock actions
   - Wallet staging for loading
   """
   def __init__(self, parent, main=None):
      super().__init__(parent)
      self.main = main
      self.walletListData = WalletList()
      self.walletIdToCheckbox = {}
      self.armoryDataDirEdit = None
      self.walletList = None
      self.noWalletsLabel = None
      self.initUI()

   def initUI(self):
      """Initialize the wallet tab UI."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      title = qtdefines.createTabTitle(self.tr('Wallet Setup'))
      mainLayout.addWidget(title)

      self.initWidgets()

      dirFrame = QtWidgets.QGroupBox(self.tr('Armory Data'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      dirLabel = QtWidgets.QLabel(self.tr("Armory Data Directory"))
      dirLabel.setToolTip(self.tr(
         'The directory where Armory will store all wallet '
         'files and settings.\n\nThis directory should be:\n'
         '\u2022 On a secure drive\n'
         '\u2022 Regularly backed up\n'
         '\u2022 Have sufficient free space'))

      dirInputLayout, browseBtn = qtdefines.createDirectoryInputLayout(
         self, self.armoryDataDirEdit, self.tr('Select Armory Data Directory'))
      browseBtn.setToolTip(self.tr(
         'Click to browse for a directory to store Armory '
         'data.\n\nChoose a location that is:\n'
         '\u2022 Secure and private\n'
         '\u2022 Has sufficient space\n'
         '\u2022 Is on a reliable drive'))

      dirGrid.addWidget(dirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      dirFrameLayout.addLayout(dirGrid)
      mainLayout.addWidget(dirFrame)

      walletFrame = QtWidgets.QGroupBox(self.tr('Available Wallets'))
      walletFrameLayout = QtWidgets.QVBoxLayout(walletFrame)
      walletFrameLayout.setContentsMargins(12, 12, 12, 12)
      walletFrameLayout.setSpacing(8)

      walletFrameLayout.addWidget(self.walletList)
      walletFrameLayout.addWidget(self.noWalletsLabel)

      mainLayout.addWidget(walletFrame)
      mainLayout.addStretch()
      self.setLayout(mainLayout)

   def initWidgets(self):
      """Initialize wallet tab widgets as instance variables."""
      self.armoryDataDirEdit = QtWidgets.QLineEdit()
      self.armoryDataDirEdit.setMinimumWidth(400)
      self.walletList = QtWidgets.QTreeWidget()
      self.walletList.setHeaderLabels(
         ['', 'Wallet ID', 'File Name', 'Type', 'Action'])
      self.walletList.setColumnWidth(0, 30)
      self.walletList.setColumnWidth(1, 120)
      self.walletList.setColumnWidth(2, 200)
      self.walletList.setColumnWidth(3, 100)
      self.walletList.setColumnWidth(4, 40)
      self.walletList.setSelectionMode(QtWidgets.QAbstractItemView.NoSelection)
      self.walletList.setIndentation(0)
      self.walletList.setRootIsDecorated(False)
      header = self.walletList.header()
      header.setDefaultAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
      self.walletList.setMinimumHeight(100)
      self.walletList.setMaximumHeight(200)
      self.noWalletsLabel = qtdefines.QRichLabel(
         self.tr('N/A'), doWrap=False,
         hAlign=QtCore.Qt.AlignHCenter, vAlign=QtCore.Qt.AlignVCenter)
      self.noWalletsLabel.setText(self.tr('N/A'), size=4, bold=True,
         color='DisableFG')
      self.noWalletsLabel.setMinimumHeight(100)
      self.noWalletsLabel.hide()

   def loadSettings(self):
      """Load wallet tab settings from configuration."""
      self.armoryDataDirEdit.setText(os.path.normpath(ARMORY_HOME_DIR))

   def loadWalletList(self):
      """Fetch and render wallet list."""
      wallets = self.walletListData.getFilteredList()
      self.walletList.clear()
      self.walletIdToCheckbox.clear()
      hasWallets = bool(wallets)
      self.walletList.setVisible(hasWallets)
      self.noWalletsLabel.setVisible(not hasWallets)
      if not hasWallets:
         return

      for walletEntry in wallets:
         item = QtWidgets.QTreeWidgetItem()
         item.setText(qtdefines.WLTLISTCOLS.WalletID, walletEntry.displayName)
         item.setText(qtdefines.WLTLISTCOLS.Filename, walletEntry.filename)
         if walletEntry.isLegacy:
            item.setText(qtdefines.WLTLISTCOLS.Type, self.tr('Legacy'))
         elif walletEntry.isEncrypted:
            item.setText(qtdefines.WLTLISTCOLS.Type, self.tr('Encrypted'))
         elif walletEntry.isReady:
            if walletEntry.watchingOnly:
               item.setText(qtdefines.WLTLISTCOLS.Type, self.tr('Ready (WO)'))
            else:
               item.setText(qtdefines.WLTLISTCOLS.Type, self.tr('Ready'))
         else:
            item.setText(qtdefines.WLTLISTCOLS.Type, self.tr('Unknown'))
         self.walletList.addTopLevelItem(item)

         if walletEntry.isReady and walletEntry.walletId:
            cell, cb = qtdefines.makeCheckboxCell(
               True, True,
               lambda state, wid=walletEntry.walletId: (
                  self.onStageCheckboxChanged(wid,
                     state == QtCore.Qt.Checked)))
            self.walletList.setItemWidget(item, qtdefines.WLTLISTCOLS.Checkbox,
               cell)
            self.walletIdToCheckbox[walletEntry.walletId] = cb
         else:
            cell, _ = qtdefines.makeCheckboxCell(False, False)
            self.walletList.setItemWidget(item, qtdefines.WLTLISTCOLS.Checkbox,
               cell)
         item.setData(qtdefines.WLTLISTCOLS.Checkbox, QtCore.Qt.UserRole,
            walletEntry)
         if walletEntry.isLegacy:
            self._addActionButton(
               item, self.tr('Migrate'),
               lambda _, entry=walletEntry: self.migrateWallet(entry))
         elif walletEntry.isEncrypted:
            self._addActionButton(
               item, self.tr('Unlock'),
               lambda _, entry=walletEntry: self.unlockWallet(entry))

   def _addActionButton(self, item, text, handler):
      """Create and add a left-aligned action button to the wallet list item."""
      btn = QtWidgets.QPushButton(text)
      btn.clicked.connect(handler)
      btnWidth = qtdefines.relaxedSizeNChar(
         self, 10)[0]
      btn.setMaximumWidth(btnWidth)
      cellWidget = QtWidgets.QWidget()
      layout = QtWidgets.QHBoxLayout(cellWidget)
      layout.setContentsMargins(2, 2, 2, 2)
      layout.addWidget(btn)
      layout.addStretch()
      self.walletList.setItemWidget(item, qtdefines.WLTLISTCOLS.Action,
         cellWidget)

   def migrateWallet(self, walletEntry):
      """Start migration for selected wallet."""
      dlg = None
      try:
         if not walletEntry.importPreview:
            raise RuntimeError(
               "Legacy wallet missing extended "
               "data from bridge")
         setupMgr = self.window()
         dlg = DlgWalletMigration(
            setupMgr, setupMgr,
            walletEntry.filename,
            walletEntry.importPreview)
         result = dlg.exec_()
         if result == QtWidgets.QDialog.Accepted:
            LOGINFO("Wallet migration accepted"
               " - refreshing list")
            self.loadWalletList()
         else:
            LOGINFO("Wallet migration cancelled")
      except Exception as e:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Migration Failed'),
            self.tr(
               'Failed to start migration: {}'
               ).format(str(e)))
      finally:
         if dlg:
            dlg.deleteLater()
            dlg = None

   def unlockWallet(self, walletEntry):
      """Unlock wallet via control header pattern."""
      walletId = walletEntry.walletId
      try:
         unlockDlg = UnlockWalletHandler(
            walletId,
            self.tr('Unlock Wallet'),
            self.window())

         def handleUnlockResult(replyObj):
            try:
               if replyObj.success:
                  LOGINFO(
                     f"Wallet {walletId} unlocked")
                  unlockDlg.accept()
                  self.loadWalletList()
               else:
                  errorMsg = replyObj.error \
                     if replyObj.error \
                     else "Unknown error"
                  LOGERROR(
                     f"Unlock {walletId} failed:"
                     f" {errorMsg}")
                  unlockDlg.reject()
                  QtWidgets.QMessageBox.warning(
                     self,
                     self.tr('Unlock Failed'),
                     self.tr(
                        'Failed to unlock '
                        'wallet: {}'
                        ).format(errorMsg))
            except Exception as e:
               LOGERROR(
                  f"Unlock callback error: {e}")
               unlockDlg.reject()

         TheBridge.wltManager.unlockControlHeader(
            walletEntry.filename,
            unlockDlg.callbackId,
            lambda x: TheSignalExecution
               .executeMethod(
                  handleUnlockResult, x))
         unlockDlg.exec_()
      except Exception as e:
         LOGERROR(
            f"Failed to unlock {walletId}: {e}")
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Unlock Failed'),
            self.tr(
               'Failed to unlock wallet: {}'
               ).format(str(e)))

   def onStageCheckboxChanged(self, walletId, checked):
      success = TheBridge.wltManager.stageWallet(
         walletId, checked)
      if not success:
         LOGERROR(f"stageWallet failed: {walletId}")
         cb = self.walletIdToCheckbox.get(walletId)
         if cb:
            cb.blockSignals(True)
            cb.setChecked(not checked)
            cb.blockSignals(False)

   def collectSettings(self):
      """Return current settings from UI as a dict."""
      return {
         'armoryPath': str(self.armoryDataDirEdit.text()),
      }

   def validate(self):
      """Validate wallet tab settings. Returns True if valid."""
      armoryPath = str(self.armoryDataDirEdit.text())
      if not os.path.exists(armoryPath):
         try:
            os.makedirs(armoryPath)
         except (OSError, PermissionError) as e:
            QtWidgets.QMessageBox.critical(
               self,
               self.tr('Error'),
               self.tr('Cannot create Armory data directory: {}').format(str(e))
            )
            return False
      return True
