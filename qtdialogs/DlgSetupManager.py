################################################################################
#                                                                              #
#  Copyright (C) 2025, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

import os

from qtpy import QtCore, QtWidgets
from armoryengine.ArmoryUtils import ARMORY_DB_DIR, ARMORY_HOME_DIR, LOGINFO, \
   LOGERROR, CLI_OPTIONS
from armoryengine.Settings import TheSettings
from armoryengine.BDM import TheBDM
from armoryengine.CppBridge import TheBridge
from armoryengine.WalletUtils import WalletList
from ui.QtExecuteSignal import TheSignalExecution

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgWalletMigration import DlgWalletMigration
from qtdialogs.DlgUnlockWallet import UnlockWalletHandler
import qtdialogs.qtdefines as qtdefines

# --- Dialog-specific constants ---
MINIMUM_DIALOG_WIDTH = 300
MINIMUM_DIALOG_HEIGHT = 500

# Core operation scenarios
SCENARIO_CORE_AUTOMATE = "Let Armory Automate It"
SCENARIO_CORE_MANUAL = "Run Manually"

# Database scenario constants
SCENARIO_DB_LOCAL = "Local Database"
SCENARIO_DB_REMOTE = "Remote Database"
SCENARIO_DB_NONE = "Offline"

# Validation limits
MAX_RAM_USAGE = 256      # Max RAM in 128MB increments (~32GB)
MAX_THREAD_COUNT = 64    # Max threads for DB operations

################################################################################
class DlgSetupManager(ArmoryDialog):
   """
   Setup Manager Dialog for Armory configuration.

   Manages:
   - Wallet settings and migration
   - Bitcoin Core configuration
   - Database settings (local/remote/none)
   """
   def __init__(self, parent=None, main=None):
      """Initialize the setup manager dialog."""
      super().__init__(parent, main)
      # UI component references
      self.tabWidget = None
      self.walletTab = None
      self.coreTab = None
      self.databaseTab = None
      self.acceptButton = None
      self.cancelButton = None
      self.bottomFrame = None
      self.walletListData = WalletList()
      self.satoshiHomePath = None
      self.satoshiBrowseButton = None
      self.scenarioCombo = None
      self.networkModeCombo = None
      self.p2pPortInput = None
      self.rpcPortInput = None
      self.armoryDataDirEdit = None
      self.walletList = None
      self.databaseDirEdit = None
      self.databaseScenarioCombo = None
      self.databaseTypeCombo = None
      self.ramUsageEdit = None
      self.threadCountEdit = None
      self.localDatabaseFrame = None
      self.remoteFrame = None
      self.remoteHostEdit = None
      self.remotePortEdit = None
      self.remoteUserEdit = None
      self.testConnectionButton = None
      self.walletIdToCheckbox = {}
      self.noWalletsLabel = None
      self.coreDirectoryFrame = None
      self.coreSettingsFrame = None
      self.dbBootstrapLabel = None
      self.setupDialogProperties()
      self.initTabs()
      self.setupMainLayout()
      self.loadSettings()
      self.connectSignals()

   def setupDialogProperties(self):
      """Configure basic dialog properties and styling."""
      self.setMinimumWidth(MINIMUM_DIALOG_WIDTH)
      self.setMinimumHeight(MINIMUM_DIALOG_HEIGHT)
      self.setWindowTitle(self.tr('Armory Setup Manager'))
      self.setWindowFlags(QtCore.Qt.Window)
      self.setModal(True)
      # Use default dialog styling (match DlgWalletMigration look)
      qtdefines.applyDialogBaseStyle(self)

   def initTabs(self):
      """Initialize all tab widgets."""
      self.tabWidget = QtWidgets.QTabWidget()
      self.tabWidget.setContentsMargins(14, 6, 14, 8)
      self.initWalletTab()
      self.initCoreTab()
      self.initDatabaseTab()
      self.tabWidget.addTab(self.walletTab, self.tr('Wallet Settings'))
      self.tabWidget.addTab(self.coreTab, self.tr('Core Settings'))
      self.tabWidget.addTab(self.databaseTab, self.tr('Database Settings'))
      if CLI_OPTIONS.offline:
         self.tabWidget.setTabEnabled(1, False)
         self.tabWidget.setTabEnabled(2, False)
         self.tabWidget.setTabText(1, self.tr('Core Settings (Offline)'))
         self.tabWidget.setTabText(2, self.tr('Database Settings (Offline)'))

   def setupMainLayout(self):
      """Set up the main layout with tabs and buttons."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setSpacing(8)
      mainLayout.setContentsMargins(0, 0, 0, 0)
      mainLayout.addWidget(self.tabWidget)
      self.createBottomButtonFrame()
      mainLayout.addWidget(self.bottomFrame)
      self.setLayout(mainLayout)

   def createBottomButtonFrame(self):
      """Create the bottom frame containing Accept/Cancel buttons."""
      self.bottomFrame = QtWidgets.QFrame()
      bottomLayout = QtWidgets.QHBoxLayout(self.bottomFrame)
      bottomLayout.setContentsMargins(14, 6, 14, 8)
      bottomLayout.setSpacing(8)
      buttonBox = QtWidgets.QDialogButtonBox()
      self.acceptButton = buttonBox.addButton(self.tr('Accept'),
         QtWidgets.QDialogButtonBox.AcceptRole)
      self.cancelButton = buttonBox.addButton(self.tr('Cancel'),
         QtWidgets.QDialogButtonBox.RejectRole)
      self.acceptButton.setFixedWidth(100)
      self.cancelButton.setFixedWidth(100)
      bottomLayout.addStretch(1)
      bottomLayout.addWidget(buttonBox)

   def connectSignals(self):
      """Connect all signals to their handlers."""
      self.acceptButton.clicked.connect(self.accept)
      self.cancelButton.clicked.connect(self.reject)
      if self.satoshiBrowseButton:
         self.satoshiBrowseButton.clicked.connect(self.browseSatoshiHome)
      if self.scenarioCombo:
         self.scenarioCombo.currentIndexChanged.connect(self.onScenarioChanged)
      if self.networkModeCombo:
         self.networkModeCombo.currentIndexChanged.connect(
            self.onNetworkModeChanged)
      if self.databaseScenarioCombo:
         self.databaseScenarioCombo.currentIndexChanged.connect(
            self.handleDatabaseScenarioChange)
      if self.testConnectionButton:
         self.testConnectionButton.clicked.connect(self.testRemoteConnection)
      self.databaseTypeCombo.currentTextChanged.connect(self.updateCliCommandDisplay)
      self.ramUsageEdit.textChanged.connect(self.updateCliCommandDisplay)
      self.threadCountEdit.textChanged.connect(self.updateCliCommandDisplay)
      self.databaseDirEdit.textChanged.connect(self.updateCliCommandDisplay)
      self.remoteHostEdit.textChanged.connect(self.updateCliCommandDisplay)
      self.remotePortEdit.textChanged.connect(self.updateCliCommandDisplay)
      self.remoteUserEdit.textChanged.connect(self.updateCliCommandDisplay)

   def accept(self):
      """Validate and save settings, then accept dialog if successful."""
      if not self.validateAllSettings():
         return
      try:
         self.saveSettings()
      except (OSError, IOError, PermissionError) as e:
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Settings Error'),
            self.tr('Failed to save settings: {}').format(str(e))
         )
         return
      super().accept()

   ###########################################################################
   def initCoreTab(self):
      """Initialize the Core settings tab (call once during setup)."""
      self.coreTab = QtWidgets.QWidget()
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      title = qtdefines.QRichLabel(
         self.tr('<span style="font-size:14pt;"><b>Core Settings</b></span>'))
      title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
      mainLayout.addWidget(title)

      self.createDirectoryFrame()
      mainLayout.addWidget(self.coreDirectoryFrame)

      self.createCoreSettingsFrame()
      mainLayout.addWidget(self.coreSettingsFrame)

      mainLayout.addStretch()
      self.coreTab.setLayout(mainLayout)

   def createDirectoryFrame(self):
      """Create the directory settings frame for the Core tab."""
      self.coreDirectoryFrame = QtWidgets.QGroupBox(self.tr('Bitcoin Core Data'))
      dirFrameLayout = QtWidgets.QVBoxLayout(self.coreDirectoryFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      coreDirLabel = QtWidgets.QLabel(self.tr("Bitcoin Core Data Directory"))
      self.satoshiHomePath = QtWidgets.QLineEdit()
      self.satoshiHomePath.setFixedWidth(400)
      self.satoshiBrowseButton = QtWidgets.QPushButton(self.tr("Browse..."))
      self.satoshiBrowseButton.setFixedWidth(100)

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      dirInputLayout.addWidget(self.satoshiHomePath)
      dirInputLayout.addWidget(self.satoshiBrowseButton)

      dirGrid.addWidget(coreDirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      dirFrameLayout.addLayout(dirGrid)

   def createCoreSettingsFrame(self):
      """Create core settings frame with operation mode and network options."""
      self.coreSettingsFrame = QtWidgets.QGroupBox(self.tr('Operation'))
      coreFrameLayout = QtWidgets.QVBoxLayout(self.coreSettingsFrame)
      coreFrameLayout.setContentsMargins(12, 12, 12, 12)
      coreFrameLayout.setSpacing(8)

      grid = QtWidgets.QGridLayout()
      grid.setSpacing(8)

      operationLabel = QtWidgets.QLabel(self.tr("Operation Mode"))
      self.scenarioCombo = QtWidgets.QComboBox()
      self.scenarioCombo.setFixedWidth(200)
      self.scenarioCombo.addItems([SCENARIO_CORE_AUTOMATE, SCENARIO_CORE_MANUAL])
      self.scenarioCombo.setEditable(False)

      networkLabel = QtWidgets.QLabel(self.tr("Network Mode"))
      self.networkModeCombo = QtWidgets.QComboBox()
      self.networkModeCombo.setFixedWidth(200)
      self.networkModeCombo.setEnabled(False)
      self.networkModeCombo.addItem(self.tr("Mainnet"))
      self.networkModeCombo.addItem(self.tr("Testnet"))
      self.networkModeCombo.addItem(self.tr("Regtest"))

      networkInfo = QtWidgets.QLabel(self.tr("(Set by command line arguments)"))
      networkInfo.setStyleSheet("color: gray; font-style: italic;")

      p2pPortLabel = QtWidgets.QLabel(self.tr("Bitcoin P2P Port"))
      self.p2pPortInput = QtWidgets.QLineEdit()
      self.p2pPortInput.setFixedWidth(100)
      self.p2pPortInput.setText("8333")

      rpcPortLabel = QtWidgets.QLabel(self.tr("RPC Port"))
      self.rpcPortInput = QtWidgets.QLineEdit()
      self.rpcPortInput.setFixedWidth(100)
      self.rpcPortInput.setEnabled(False)
      self.rpcPortInput.setToolTip(
         "Standard Bitcoin Core RPC port (not configurable in Armory)")

      grid.addWidget(operationLabel, 0, 0)
      grid.addWidget(self.scenarioCombo, 0, 1)
      grid.addWidget(networkLabel, 1, 0)
      grid.addWidget(self.networkModeCombo, 1, 1)
      grid.addWidget(networkInfo, 1, 2)
      grid.addWidget(p2pPortLabel, 2, 0)
      grid.addWidget(self.p2pPortInput, 2, 1)
      grid.addWidget(rpcPortLabel, 3, 0)
      grid.addWidget(self.rpcPortInput, 3, 1)
      grid.setColumnStretch(1, 1)

      coreFrameLayout.addLayout(grid)

   def onScenarioChanged(self, index):
      """Handle changes to the scenario selection."""
      scenario = self.scenarioCombo.itemText(index)
      if scenario == SCENARIO_CORE_AUTOMATE or scenario == SCENARIO_CORE_MANUAL:
         self.p2pPortInput.setEnabled(False)
         self.rpcPortInput.setEnabled(False)

   def showEvent(self, event):
      """Handle show event to ensure the dialog is properly displayed."""
      super().showEvent(event)
      flags = self.windowState()
      flags = flags & ~QtCore.Qt.WindowMinimized
      flags = flags | QtCore.Qt.WindowActive
      self.setWindowState(flags)
      self.raise_()

   def onBridgeReady(self):
      self.loadWalletList()

   def browseSatoshiHome(self):
      """Open a directory dialog to select the Bitcoin Core data directory."""
      directory = QtWidgets.QFileDialog.getExistingDirectory(
         self,
         self.tr('Select Bitcoin Core Data Directory'),
         os.path.expanduser('~')
      )
      if directory:
         self.satoshiHomePath.setText(directory)

   def browseDirDialog(self, lineEdit):
      """Open directory chooser into the given QLineEdit using shared helper."""
      qtdefines.selectDirectoryForQLineEdit(
         self, lineEdit, title=self.tr('Select Directory'))

   def onNetworkModeChanged(self, index):
      """Handle changes to the network mode selection."""
      # NOTE: Network mode is set by CLI args only and cannot be changed in UI
      # This method should never be called since combo is disabled
      raise RuntimeError("Network mode cannot be changed - it's set by CLI arguments only")

   ###########################################################################
   def initWalletTab(self):
      """Initialize the wallet settings tab (call once during setup)."""
      self.walletTab = QtWidgets.QWidget()
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      title = qtdefines.QRichLabel(self.tr(
         '<span style="font-size:14pt;"><b>Wallet Setup</b></span>'))
      title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
      mainLayout.addWidget(title)

      self.initWalletWidgets()

      dirFrame = QtWidgets.QGroupBox(self.tr('Armory Data'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      dirLabel = QtWidgets.QLabel(self.tr("Armory Data Directory"))
      dirLabel.setToolTip('The directory where Armory will store all wallet '
         'files and settings.\n\nThis directory should be:\n• On a secure '
         'drive\n• Regularly backed up\n• Have sufficient free space')

      browseBtn = QtWidgets.QPushButton(self.tr("Browse..."))
      browseBtn.setFixedWidth(100)
      browseBtn.clicked.connect(lambda: self.browseDirDialog(self.armoryDataDirEdit))
      browseBtn.setToolTip('Click to browse for a directory to store Armory '
         'data.\n\nChoose a location that is:\n• Secure and private\n• Has '
         'sufficient space\n• Is on a reliable drive')

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      dirInputLayout.addWidget(self.armoryDataDirEdit)
      dirInputLayout.addWidget(browseBtn)

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
      self.walletTab.setLayout(mainLayout)

   def initWalletWidgets(self):
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

   def registerWidgetActivateTime(self, widget):
      """Stub for entropy collection - no-op during setup since no main window exists yet."""
      pass

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
            self.walletList.setItemWidget(item, qtdefines.WLTLISTCOLS.Checkbox, cell)
            self.walletIdToCheckbox[walletEntry.walletId] = cb
         else:
            cell, _ = qtdefines.makeCheckboxCell(False, False)
            self.walletList.setItemWidget(item, qtdefines.WLTLISTCOLS.Checkbox, cell)
         item.setData(qtdefines.WLTLISTCOLS.Checkbox, QtCore.Qt.UserRole, walletEntry)
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
      btn.setMaximumWidth(60)
      cellWidget = QtWidgets.QWidget()
      layout = QtWidgets.QHBoxLayout(cellWidget)
      layout.setContentsMargins(2, 2, 2, 2)
      layout.addWidget(btn)
      layout.addStretch()
      self.walletList.setItemWidget(item, qtdefines.WLTLISTCOLS.Action, cellWidget)

   def migrateWallet(self, walletEntry):
      """Start migration for selected wallet."""
      dlg = None
      try:
         if not walletEntry.importPreview:
            raise Exception("Legacy wallet missing extended data from bridge")
         mainRef = self.main if self.main else self
         dlg = DlgWalletMigration(self, mainRef, walletEntry.filename,
            walletEntry.importPreview)
         result = dlg.exec_()
         if result == QtWidgets.QDialog.Accepted:
            LOGINFO("Wallet migration dialog accepted - refreshing wallet list")
            self.loadWalletList()
            LOGINFO("Wallet list refreshed after migration")
         else:
            LOGINFO("Wallet migration cancelled by user")

      except Exception as e:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Migration Failed'),
            self.tr('Failed to start migration: {}').format(str(e))
         )
      finally:
         # Ensure dialog is cleaned up
         if dlg:
            dlg.deleteLater()
            dlg = None

   def unlockWallet(self, walletEntry):
      """Unlock wallet using proper unlock control header pattern."""
      walletId = walletEntry.walletId
      try:
         unlockDlg = UnlockWalletHandler(walletId, self.tr('Unlock Wallet'), self)

         def handleUnlockResult(replyObj):
            """Handle unlock control header reply."""
            try:
               if replyObj.success:
                  LOGINFO(f"Wallet {walletId} unlocked successfully")
                  unlockDlg.accept()
                  self.loadWalletList()
               else:
                  error_msg = replyObj.error if replyObj.error else "Unknown error"
                  LOGINFO(f"Failed to unlock wallet {walletId}: {error_msg}")
                  unlockDlg.reject()
                  QtWidgets.QMessageBox.warning(
                     self,
                     self.tr('Unlock Failed'),
                     self.tr('Failed to unlock wallet: {}').format(error_msg))
            except Exception as e:
               LOGINFO(f"Unlock callback error: {e}")
               unlockDlg.reject()

         TheBridge.wltManager.unlockControlHeader(
            walletEntry.filename,
            unlockDlg.callbackId,
            lambda x: TheSignalExecution.executeMethod(handleUnlockResult, x))
         unlockDlg.exec_()

      except Exception as e:
         LOGINFO(f"Failed to unlock wallet {walletId}: {e}")
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Unlock Failed'),
            self.tr('Failed to unlock wallet: {}').format(str(e)))

   ###########################################################################
   def initDatabaseTab(self):
      """Initialize the database settings tab (call once during setup)."""
      self.databaseTab = QtWidgets.QWidget()
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      title = qtdefines.QRichLabel(self.tr(
         '<span style="font-size:14pt;"><b>Database Settings</b></span>'))
      title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
      mainLayout.addWidget(title)

      self.initDatabaseWidgets()

      dirFrame = QtWidgets.QGroupBox(self.tr('Database Directory'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      dbDirLabel = QtWidgets.QLabel(self.tr("Location"))
      dbDirButton = QtWidgets.QPushButton(self.tr("Browse..."))
      dbDirButton.setFixedWidth(100)
      dbDirButton.clicked.connect(lambda: self.browseDirDialog(self.databaseDirEdit))

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      dirInputLayout.addWidget(self.databaseDirEdit)
      dirInputLayout.addWidget(dbDirButton)

      dirGrid.addWidget(dbDirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      dirFrameLayout.addLayout(dirGrid)
      mainLayout.addWidget(dirFrame)

      # Database scenario section
      scenarioFrame = QtWidgets.QGroupBox(self.tr('Database Scenario'))
      scenarioLayout = QtWidgets.QVBoxLayout(scenarioFrame)
      scenarioLayout.setContentsMargins(12, 12, 12, 12)
      scenarioLayout.setSpacing(8)

      scenarioGrid = QtWidgets.QGridLayout()
      scenarioGrid.setSpacing(8)

      dbScenarioLabel = QtWidgets.QLabel(self.tr("Mode"))

      scenarioGrid.addWidget(dbScenarioLabel, 0, 0)
      scenarioGrid.addWidget(self.databaseScenarioCombo, 0, 1)
      scenarioGrid.setColumnStretch(1, 1)

      scenarioLayout.addLayout(scenarioGrid)
      mainLayout.addWidget(scenarioFrame)

      localDbLayout = QtWidgets.QVBoxLayout(self.localDatabaseFrame)
      localDbLayout.setContentsMargins(12, 12, 12, 12)
      localDbLayout.setSpacing(8)

      localDbGrid = QtWidgets.QGridLayout()
      localDbGrid.setSpacing(8)

      dbTypeLabel = QtWidgets.QLabel(self.tr("Database Type"))
      ramLabel = QtWidgets.QLabel(self.tr("RAM Usage (MB)"))
      threadLabel = QtWidgets.QLabel(self.tr("Thread Count"))

      localDbGrid.addWidget(dbTypeLabel, 0, 0)
      localDbGrid.addWidget(self.databaseTypeCombo, 0, 1)
      localDbGrid.addWidget(ramLabel, 1, 0)
      localDbGrid.addWidget(self.ramUsageEdit, 1, 1)
      localDbGrid.addWidget(threadLabel, 2, 0)
      localDbGrid.addWidget(self.threadCountEdit, 2, 1)
      localDbGrid.setColumnStretch(1, 1)

      localDbLayout.addLayout(localDbGrid)
      mainLayout.addWidget(self.localDatabaseFrame)

      remoteLayout = QtWidgets.QVBoxLayout(self.remoteFrame)
      remoteLayout.setContentsMargins(12, 12, 12, 12)
      remoteLayout.setSpacing(8)

      remoteGrid = QtWidgets.QGridLayout()
      remoteGrid.setSpacing(8)

      hostLabel = QtWidgets.QLabel(self.tr("Remote Host"))
      portLabel = QtWidgets.QLabel(self.tr("Remote Port"))
      userLabel = QtWidgets.QLabel(self.tr("Username"))
      passLabel = QtWidgets.QLabel(self.tr("Password (handled by bridge)"))
      passInfo = QtWidgets.QLabel(
         self.tr("Bridge will prompt for password when needed"))
      passInfo.setStyleSheet("color: gray; font-style: italic;")

      remoteGrid.addWidget(hostLabel, 0, 0)
      remoteGrid.addWidget(self.remoteHostEdit, 0, 1)
      remoteGrid.addWidget(portLabel, 1, 0)
      remoteGrid.addWidget(self.remotePortEdit, 1, 1)
      remoteGrid.addWidget(userLabel, 2, 0)
      remoteGrid.addWidget(self.remoteUserEdit, 2, 1)
      remoteGrid.addWidget(passLabel, 3, 0)
      remoteGrid.addWidget(passInfo, 3, 1)
      remoteGrid.setColumnStretch(1, 1)

      remoteLayout.addLayout(remoteGrid)

      self.testConnectionButton = QtWidgets.QPushButton(
         self.tr("Test Connection"))
      self.testConnectionButton.setFixedWidth(200)
      self.testConnectionButton.clicked.connect(self.testRemoteConnection)

      buttonLayout = QtWidgets.QHBoxLayout()
      buttonLayout.addStretch()
      buttonLayout.addWidget(self.testConnectionButton)
      buttonLayout.addStretch()
      remoteLayout.addLayout(buttonLayout)

      mainLayout.addWidget(self.remoteFrame)

      cliFrame = QtWidgets.QGroupBox(self.tr('Generated Command Line'))
      cliLayout = QtWidgets.QVBoxLayout(cliFrame)
      cliLayout.setContentsMargins(12, 12, 12, 12)
      cliLayout.addWidget(self.cliCommandLabel)
      mainLayout.addWidget(cliFrame)

      mainLayout.addStretch()

      self.remoteFrame.hide()
      self.updateCliCommandDisplay()

      self.databaseTab.setLayout(mainLayout)

   def initDatabaseWidgets(self):
      """Initialize database tab widgets as instance variables."""
      self.databaseDirEdit = QtWidgets.QLineEdit()
      self.databaseDirEdit.setMinimumWidth(400)
      self.databaseScenarioCombo = QtWidgets.QComboBox()
      self.databaseScenarioCombo.setFixedWidth(200)
      self.databaseScenarioCombo.addItems([
         SCENARIO_DB_LOCAL, SCENARIO_DB_REMOTE, SCENARIO_DB_NONE])
      self.localDatabaseFrame = QtWidgets.QGroupBox(
         self.tr('Local Database Configuration'))
      self.databaseTypeCombo = QtWidgets.QComboBox()
      self.databaseTypeCombo.setFixedWidth(200)
      self.databaseTypeCombo.addItems(["Full Database", "Supernode"])
      self.ramUsageEdit = QtWidgets.QLineEdit()
      self.ramUsageEdit.setFixedWidth(100)
      self.threadCountEdit = QtWidgets.QLineEdit()
      self.threadCountEdit.setFixedWidth(100)
      self.remoteFrame = QtWidgets.QGroupBox(self.tr('Remote Connection'))
      self.remoteHostEdit = QtWidgets.QLineEdit()
      self.remoteHostEdit.setFixedWidth(200)
      self.remotePortEdit = QtWidgets.QLineEdit()
      self.remotePortEdit.setFixedWidth(100)
      self.remoteUserEdit = QtWidgets.QLineEdit()
      self.remoteUserEdit.setFixedWidth(200)
      self.cliCommandLabel = QtWidgets.QLabel()
      self.cliCommandLabel.setWordWrap(True)
      self.cliCommandLabel.setStyleSheet(
         "background-color: #f0f0f0; border: 1px solid #ccc; padding: 8px; "
         "font-family: monospace; font-size: 10pt; color: #333;")
      self.cliCommandLabel.setText("CLI Command: (will be generated based on settings)")
      self.cliCommandLabel.setMinimumHeight(60)

   def updateCliCommandDisplay(self):
      """Update the CLI command display based on current database settings."""
      dbScenario = self.databaseScenarioCombo.currentText()
      if dbScenario == SCENARIO_DB_LOCAL:
         dbType = self.databaseTypeCombo.currentText()
         ramUsage = self.ramUsageEdit.text() or '4'
         threadCount = self.threadCountEdit.text() or '4'
         dbDir = self.databaseDirEdit.text() or '/path/to/db'
         dbTypeArg = 'DB_SUPER' if dbType == 'Supernode' else 'DB_FULL'
         cmd_parts = [
            'ArmoryDB',
            f'--db-type={dbTypeArg}',
            f'--ram-usage={ramUsage}',
            f'--thread-count={threadCount}',
            f'--datadir="{dbDir}"'
         ]
      elif dbScenario == SCENARIO_DB_REMOTE:
         host = self.remoteHostEdit.text() or 'localhost'
         port = self.remotePortEdit.text() or '9001'

         cmd_parts = [
            'Armory',
            f'--armorydb-ip={host}',
            f'--armorydb-port={port}'
         ]
      else:
         cmd_parts = ['ArmoryDB', '--offline']
      command_line = ' '.join(cmd_parts)
      self.cliCommandLabel.setText(f"CLI Command: {command_line}")

   def handleDatabaseScenarioChange(self, index):
      """Handle changes to the database scenario selection."""
      dbScenario = self.databaseScenarioCombo.itemText(index)
      isLocal = dbScenario == SCENARIO_DB_LOCAL
      isRemote = dbScenario == SCENARIO_DB_REMOTE
      self.localDatabaseFrame.setVisible(isLocal)
      self.remoteFrame.setVisible(isRemote)
      self.databaseDirEdit.setEnabled(isLocal)
      self.databaseDirEdit.parentWidget().setEnabled(True)
      self.updateCliCommandDisplay()

   def testRemoteConnection(self):
      """Test remote database connection without setting up the database."""
      host = self.remoteHostEdit.text()
      port = self.remotePortEdit.text()
      user = self.remoteUserEdit.text()

      if not host or not port:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Missing Information'),
            self.tr('Please provide host and port for connection test.')
         )
         return

      QtWidgets.QMessageBox.information(
         self,
         self.tr('Connection Test'),
         self.tr('Connection test functionality needs to be implemented '
            'in bridge.\nHost: {}\nPort: {}').format(host, port)
      )

   ###########################################################################
   def loadSettings(self):
      """Load settings from configuration including dir paths and db settings."""
      # Directory paths
      btcDir = TheBDM.btcdir
      self.satoshiHomePath.setText(os.path.normpath(btcDir))
      self.armoryDataDirEdit.setText(os.path.normpath(ARMORY_HOME_DIR))
      self.databaseDirEdit.setText(os.path.normpath(ARMORY_DB_DIR))

      hasCoreSettings = bool(self.satoshiHomePath.text() and
         os.path.exists(self.satoshiHomePath.text()))
      self.scenarioCombo.setCurrentText(
         SCENARIO_CORE_AUTOMATE if hasCoreSettings else SCENARIO_CORE_MANUAL)

      # Database configuration
      dbScenario = TheSettings.getSettingOrSetDefault(
         'DBScenario', 'Local Database')
      self.databaseScenarioCombo.setCurrentText(dbScenario)

      # Check if database has already been bootstrapped
      dbIsBootstrapped = False
      if os.path.exists(ARMORY_DB_DIR):
         dbFiles = os.listdir(ARMORY_DB_DIR)
         dbIsBootstrapped = any(f.endswith('.db') or f.endswith('.ldb')
            for f in dbFiles)

      if dbIsBootstrapped:
         self.databaseScenarioCombo.setEnabled(False)
         if self.dbBootstrapLabel is None:
            self.dbBootstrapLabel = QtWidgets.QLabel(
               self.tr("(Database already bootstrapped - cannot change mode)"))
            self.dbBootstrapLabel.setStyleSheet("color: orange; font-style: italic;")
            layout = self.databaseScenarioCombo.parent().layout()
            if layout:
               layout.addWidget(self.dbBootstrapLabel)
      self.handleDatabaseScenarioChange(
         self.databaseScenarioCombo.currentIndex())
      self.remoteFrame.setVisible(False)

      # Scenario-specific settings
      dbScenario = self.databaseScenarioCombo.currentText()
      if dbScenario == SCENARIO_DB_LOCAL:
         dbTypeSetting = TheSettings.getSettingOrSetDefault('DBType', 'DB_FULL')
         if dbTypeSetting == 'DB_SUPER':
            self.databaseTypeCombo.setCurrentText('Supernode')
         else:
            self.databaseTypeCombo.setCurrentText('Full Database')
         self.ramUsageEdit.setText(
            str(TheSettings.getSettingOrSetDefault('RAMUsage', 50)))
         self.threadCountEdit.setText(
            str(TheSettings.getSettingOrSetDefault('ThreadCount', 4)))
      elif dbScenario == SCENARIO_DB_REMOTE:
         savedHost = TheSettings.get('RemoteDBHost')
         savedPort = TheSettings.get('RemoteDBPort')
         self.remoteHostEdit.setText(savedHost if savedHost else '')
         self.remotePortEdit.setText(str(savedPort) if savedPort else '')
         self.remoteUserEdit.clear()

   def validateSettings(self):
      """Validate directory paths group and create as needed."""
      paths = self._collectPathsGroup()
      corePath = paths['core']
      armoryPath = paths['armory']
      dbPath = paths['db']

      if not os.path.exists(corePath):
         reply = QtWidgets.QMessageBox.warning(
            self,
            self.tr('Invalid Directory'),
            self.tr('Bitcoin Core data directory does not exist. '
               'Please select a valid directory.'),
            QtWidgets.QMessageBox.Ok | QtWidgets.QMessageBox.Cancel)
         if reply == QtWidgets.QMessageBox.Ok:
            newDir = QtWidgets.QFileDialog.getExistingDirectory(
               self,
               self.tr('Select Bitcoin Core Data Directory'),
               os.path.expanduser('~'))
            if newDir:
               self.satoshiHomePath.setText(newDir)
               return self.validateSettings()
         return False

      if not os.path.exists(armoryPath):
         try:
            os.makedirs(armoryPath)
         except Exception as e:
            QtWidgets.QMessageBox.critical(
               self,
               self.tr('Error'),
               self.tr('Cannot create Armory data directory: {}').format(str(e))
            )
            return False

      if not os.path.exists(dbPath):
         try:
            os.makedirs(dbPath)
         except Exception as e:
            QtWidgets.QMessageBox.critical(
               self,
               self.tr('Error'),
               self.tr('Cannot create database directory: {}').format(str(e))
            )
            return False

      return True

   def validateAllSettings(self):
      """Run grouped validators and surface the first failing message."""
      if not self.validateSettings():
         return False
      if not self._validateCoreGroup():
         return False
      if not self._validateDbGroup():
         return False
      return True

   def _validateCoreGroup(self):
      scenario = self.scenarioCombo.currentText()
      scenarioOk = scenario in (SCENARIO_CORE_AUTOMATE, SCENARIO_CORE_MANUAL)
      modeOk = str(self.networkModeCombo.currentText()) in (
         'Mainnet', 'Testnet', 'Regtest')
      if not (scenarioOk and modeOk):
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Invalid Core Settings'),
            self.tr('Please select a valid scenario and network mode.')
         )
         return False
      self.p2pPortInput.setEnabled(False)
      self.rpcPortInput.setEnabled(False)
      return True

   def _validateDbGroup(self):
      dbScenario = self.databaseScenarioCombo.currentText()
      if dbScenario == SCENARIO_DB_LOCAL:
         ramText = str(self.ramUsageEdit.text()).strip()
         threadText = str(self.threadCountEdit.text()).strip()
         if ramText:
            try:
               ram = int(ramText)
               if ram < 1 or ram > MAX_RAM_USAGE:
                  raise ValueError()
            except ValueError:
               msg = self.tr('RAM usage must be an integer between 1 and {}.')
               QtWidgets.QMessageBox.warning(self, self.tr('Invalid RAM Usage'),
                  msg.format(MAX_RAM_USAGE))
               return False
         if threadText:
            try:
               threads = int(threadText)
               if threads < 1 or threads > MAX_THREAD_COUNT:
                  raise ValueError()
            except ValueError:
               msg = self.tr('Thread count must be an integer between 1 and {}.')
               QtWidgets.QMessageBox.warning(
                  self, self.tr('Invalid Thread Count'),
                  msg.format(MAX_THREAD_COUNT))
               return False
      elif dbScenario == SCENARIO_DB_REMOTE:
         host = str(self.remoteHostEdit.text())
         portText = str(self.remotePortEdit.text())
         if not host or not portText:
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Missing Remote DB Info'),
               self.tr('Please provide host and port for the remote database.'))
            return False
         try:
            port = int(portText)
            if port < 1 or port > 65535:
               raise ValueError()
         except ValueError:
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Invalid Port'),
               self.tr('Remote DB port must be an integer between 1 and 65535.'))
            return False
      return True

   def onStageCheckboxChanged(self, walletId, checked):
      """Handle staging checkbox with proper error recovery."""
      try:
         TheBridge.wltManager.stageWallet(walletId, checked)
      except Exception as e:
         cb = self.walletIdToCheckbox.get(walletId)
         if cb:
            cb.blockSignals(True)
            cb.setChecked(not checked)
            cb.blockSignals(False)
         QtWidgets.QMessageBox.warning(
            self, self.tr('Stage Failed'), str(e))

   def _collectPathsGroup(self):
      """Return current paths from UI as a dict."""
      return {
         'core': str(self.satoshiHomePath.text()),
         'armory': str(self.armoryDataDirEdit.text()),
         'db': str(self.databaseDirEdit.text()),
      }

   def _collectCoreGroup(self):
      """Return current core settings from UI as a dict."""
      return {
         'networkMode': str(self.networkModeCombo.currentText()),
         'manageSatoshi': (self.scenarioCombo.currentText() == SCENARIO_CORE_AUTOMATE),
      }

   def _collectDbGroup(self):
      """Return current database config from UI as a dict."""
      dbScenario = self.databaseScenarioCombo.currentText()
      isLocal = dbScenario == SCENARIO_DB_LOCAL
      isRemote = dbScenario == SCENARIO_DB_REMOTE
      return {
         'scenario': str(self.databaseScenarioCombo.currentText()),
         'typeDisp': str(self.databaseTypeCombo.currentText()) if isLocal else '',
         'remoteHost': str(self.remoteHostEdit.text()) if isRemote else '',
         'remotePort': str(self.remotePortEdit.text()) if isRemote else '',
         'ram': str(self.ramUsageEdit.text()) if isLocal else '',
         'threads': str(self.threadCountEdit.text()) if isLocal else '',
      }

   def _setSettingIfChanged(self, name, value):
      """Only write setting to file if the value has actually changed."""
      try:
         currentValue = TheSettings.get(name)
         if currentValue != value:
            TheSettings.set(name, value)
      except Exception:
         TheSettings.set(name, value)

   def _applySettingsGroups(self, paths, core, db):
      """Persist settings and propagate runtime options from grouped dicts."""
      self._setSettingIfChanged('CoreDataDir', paths['core'])
      self._setSettingIfChanged('ArmoryDataDir', paths['armory'])
      self._setSettingIfChanged('DBDir', paths['db'])
      self._setSettingIfChanged('SatoshiDatadir', paths['core'])
      self._setSettingIfChanged('ManageSatoshi', core['manageSatoshi'])
      self._setSettingIfChanged('NetworkMode', core['networkMode'])
      dbScenario = self.databaseScenarioCombo.currentText()
      self._setSettingIfChanged('DBScenario', db['scenario'])
      if dbScenario == SCENARIO_DB_LOCAL:
         dbTypeVal = 'DB_SUPER' if db['typeDisp'] == 'Supernode' else 'DB_FULL'
         self._setSettingIfChanged('DBType', dbTypeVal)
         if db['ram']:
            self._setSettingIfChanged('RAMUsage', int(db['ram']))
         if db['threads']:
            self._setSettingIfChanged('ThreadCount', int(db['threads']))
      elif dbScenario == SCENARIO_DB_REMOTE:
         if db['remoteHost']:
            self._setSettingIfChanged('RemoteDBHost', db['remoteHost'])
         if db['remotePort']:
            self._setSettingIfChanged('RemoteDBPort', db['remotePort'])
      if self.main:
         self.main.setSatoshiPaths()

   def saveSettings(self):
      """Save directory paths, database settings, and update bridge args."""
      paths = self._collectPathsGroup()
      core = self._collectCoreGroup()
      db = self._collectDbGroup()
      self._applySettingsGroups(paths, core, db)
