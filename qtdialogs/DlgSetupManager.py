##############################################################################
#                                                                            #
#  Copyright (C) 2025, goatpig                                               #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

import os
from qtpy import QtCore, QtWidgets
from armoryengine.ArmoryUtils import BTC_HOME_DIR, ARMORY_DB_DIR, ARMORY_HOME_DIR, \
   CLI_OPTIONS, LOGEXCEPT, LOGINFO
from armoryengine.Settings import TheSettings
from armoryengine.CppBridge import TheBridge
from armoryengine.BDM import TheBDM
from ui.QtExecuteSignal import TheSignalExecution

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgWalletMigration import DlgWalletMigration
from qtdialogs.DlgUnlockWallet import UnlockWalletHandler
from qtdialogs.qtdefines import QRichLabel, applyDialogBaseStyle, makeCenteredCell, \
   makeCheckboxCell, makeButtonCell, addPlaceholderRow

# --- Dialog-specific constants ---
MINIMUM_DIALOG_WIDTH = 350
MINIMUM_DIALOG_HEIGHT = 500

###############################################################################
class DlgSetupManager(ArmoryDialog):
   def __init__(self, parent=None, main=None, testing=False):
      """Initialize the setup manager dialog."""
      super().__init__(parent)

      self.initMemberVariables(main)
      self.setupDialogProperties()
      self.initTabs()
      self.setupMainLayout()
      self.loadSettings(testing)
      self.connectSignals()
      # Initial populate will be done by onBridgeReady callback
      # Check if bridge is already ready, otherwise wait for callback
      try:
         from armoryengine.CppBridge import TheBridge
         if TheBridge.bridgeSocket.bip15xConnection.ready():
            self.loadWalletList()
      except Exception:
         # Bridge not ready yet, will be called by onBridgeReady callback
         pass

   def initMemberVariables(self, main):
      """Initialize all member variables with default values."""
      self.main = main
      # Visual custom styles removed; use standard Qt widgets
      # Core lifecycle is handled by the bridge; no local process object

      # UI component references
      self.tabWidget = None
      self.walletTab = None
      self.coreTab = None
      self.databaseTab = None
      self.acceptButton = None
      self.cancelButton = None

      # Widget references for settings
      self.satoshiHomePath = None
      self.satoshiBrowseButton = None
      self.scenarioCombo = None
      self.networkModeCombo = None
      self.p2pPortInput = None
      self.rpcPortInput = None
      self.armoryDataDirEdit = None
      self.walletList = None
      self.refreshButton = None
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
      self.remotePasswordEdit = None
      self.testConnectionButton = None
      self.walletIdToCheckbox = {}
      self.bridgeReady = False

   def setupDialogProperties(self):
      """Configure basic dialog properties and styling."""
      self.setMinimumWidth(MINIMUM_DIALOG_WIDTH)
      self.setMinimumHeight(MINIMUM_DIALOG_HEIGHT)
      self.setWindowTitle(self.tr('Armory Setup Manager'))
      self.setWindowFlags(QtCore.Qt.Window)
      self.setModal(True)
      # Use default dialog styling (match DlgWalletMigration look)
      try:
         applyDialogBaseStyle(self)
      except Exception:
         pass

   def initTabs(self):
      """Initialize all tab widgets."""
      try:
         # Create and style the tab container
         self.tabWidget = QtWidgets.QTabWidget()
         self.tabWidget.setContentsMargins(14, 6, 14, 8)

         # Initialize individual tab widgets
         # build wallet tab
         self.walletTab = self.createWalletTab()

         # build core tab
         self.coreTab = self.createCoreTab()

         # build database tab
         self.databaseTab = self.createDatabaseTab()

         # Configure tab order
         self.tabWidget.addTab(self.walletTab, self.tr('Wallet Settings'))
         self.tabWidget.addTab(self.coreTab, self.tr('Core Settings'))
         self.tabWidget.addTab(self.databaseTab, self.tr('Database Settings'))

         # tabs created

      except Exception as e:
         # bubble up; upstream logger handles it
         raise

   def setupMainLayout(self):
      """Set up the main layout with tabs and buttons."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setSpacing(8)
      mainLayout.setContentsMargins(0, 0, 0, 0)

      # Add tab widget to main layout
      mainLayout.addWidget(self.tabWidget)

      # Create bottom frame for buttons
      bottomFrame = self.createBottomButtonFrame()
      mainLayout.addWidget(bottomFrame)

      self.setLayout(mainLayout)

   def createBottomButtonFrame(self):
      """Create the bottom frame containing Accept/Cancel buttons."""
      bottomFrame = QtWidgets.QFrame()
      bottomLayout = QtWidgets.QHBoxLayout(bottomFrame)
      bottomLayout.setContentsMargins(14, 6, 14, 8)
      bottomLayout.setSpacing(8)

      # Configure Accept/Cancel buttons
      buttonBox = QtWidgets.QDialogButtonBox()
      self.acceptButton = buttonBox.addButton(self.tr('Accept'),
         QtWidgets.QDialogButtonBox.AcceptRole)
      self.cancelButton = buttonBox.addButton(self.tr('Cancel'), 
         QtWidgets.QDialogButtonBox.RejectRole)

      # Default button styling (no custom stylesheet)
      self.acceptButton.setFixedWidth(100)
      self.cancelButton.setFixedWidth(100)

      bottomLayout.addStretch(1)
      bottomLayout.addWidget(buttonBox)

      return bottomFrame

   def connectSignals(self):
      """Connect all signals to their handlers."""
      # Dialog buttons
      self.acceptButton.clicked.connect(self.accept)
      self.cancelButton.clicked.connect(self.reject)

      # Core tab signals
      if self.satoshiBrowseButton:
         self.satoshiBrowseButton.clicked.connect(self.browseSatoshiHome)
      if self.scenarioCombo:
         self.scenarioCombo.currentIndexChanged.connect(self.scenarioChanged)
      if self.networkModeCombo:
         self.networkModeCombo.currentIndexChanged.connect(
            self.networkModeChanged)

      # Wallet tab signals  
      if self.refreshButton:
         self.refreshButton.clicked.connect(self.loadWalletList)

      # Database tab signals
      if self.databaseScenarioCombo:
         self.databaseScenarioCombo.currentIndexChanged.connect(self.handleDatabaseScenarioChange)
      if self.testConnectionButton:
         self.testConnectionButton.clicked.connect(self.testRemoteConnection)

   def accept(self):
      """Validate and save settings, then accept dialog if successful."""
      try:
         # Validate grouped settings with local sanity checks
         if not self.validateAllSettings():
            return

         # Persist settings
         self.saveSettings()

         # Mark setup as completed and close dialog
         TheSettings.set('SetupManagerCompleted', True)
         super(DlgSetupManager, self).accept()
      except Exception as e:
         LOGEXCEPT("Error during accept: %s", str(e))
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Error'),
            self.tr('Failed to save settings: {}').format(str(e))
         )

   def reject(self):
      """Handle dialog rejection without marking setup as completed."""
      super(DlgSetupManager, self).reject()
      # Don't set SetupManagerCompleted to True when rejected
      TheSettings.set('SetupManagerCompleted', False)

   def closeEvent(self, event):
      """Handle dialog close event."""
      super().closeEvent(event)

   def createCoreTab(self):
      """Create the Core settings tab with directory and network configuration."""
      try:
         tab = QtWidgets.QWidget()
         mainLayout = QtWidgets.QVBoxLayout()
         mainLayout.setContentsMargins(14, 6, 14, 8)
         mainLayout.setSpacing(8)

         # Header
         title = QRichLabel(self.tr('<span style="font-size:14pt;"><b>Core Settings</b></span>'))
         title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
         mainLayout.addWidget(title)
         # Remove decorative horizontal lines for a cleaner look

         # Directory Settings Frame
         dirFrame = self.createDirectoryFrame()
         mainLayout.addWidget(dirFrame)

         # Core Settings Frame  
         coreFrame = self.createCoreSettingsFrame()
         mainLayout.addWidget(coreFrame)

         # No footer line

         mainLayout.addStretch()
         tab.setLayout(mainLayout)

         # core tab created
         return tab

      except Exception as e:
         raise

   def createDirectoryFrame(self):
      """Create the directory settings frame for the Core tab."""
      dirFrame = QtWidgets.QGroupBox(self.tr('Bitcoin Core Data'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      # Create grid layout for directory settings
      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      # Bitcoin Core Data Directory
      coreDirLabel = QtWidgets.QLabel(self.tr("Bitcoin Core Data Directory"))

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      self.satoshiHomePath = QtWidgets.QLineEdit()
      self.satoshiHomePath.setFixedWidth(400)
      self.satoshiBrowseButton = QtWidgets.QPushButton(self.tr("Browse..."))
      self.satoshiBrowseButton.setFixedWidth(100)

      dirInputLayout.addWidget(self.satoshiHomePath)
      dirInputLayout.addWidget(self.satoshiBrowseButton)

      # Add directory settings to grid
      dirGrid.addWidget(coreDirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      # Add grid to directory frame
      dirFrameLayout.addLayout(dirGrid)
      return dirFrame

   def createCoreSettingsFrame(self):
      """Create the core settings frame with operation mode and network options."""
      coreFrame = QtWidgets.QGroupBox(self.tr('Operation'))
      coreFrameLayout = QtWidgets.QVBoxLayout(coreFrame)
      coreFrameLayout.setContentsMargins(12, 12, 12, 12)
      coreFrameLayout.setSpacing(8)

      # Create grid layout for core settings
      grid = QtWidgets.QGridLayout()
      grid.setSpacing(8)

      # Operation Mode
      operationLabel = QtWidgets.QLabel(self.tr("Operation Mode"))
      self.scenarioCombo = QtWidgets.QComboBox()
      self.scenarioCombo.setFixedWidth(200)
      self.scenarioCombo.addItem(self.tr("Let Armory Automate It"))
      self.scenarioCombo.addItem(self.tr("Run Manually"))
      self.scenarioCombo.setEditable(False)

      # Network Mode
      networkLabel = QtWidgets.QLabel(self.tr("Network Mode"))
      self.networkModeCombo = QtWidgets.QComboBox()
      self.networkModeCombo.setFixedWidth(200)
      self.networkModeCombo.addItem(self.tr("Mainnet"))
      self.networkModeCombo.addItem(self.tr("Testnet"))
      self.networkModeCombo.addItem(self.tr("Regtest"))

      # Bitcoin P2P Port
      p2pPortLabel = QtWidgets.QLabel(self.tr("Bitcoin P2P Port"))
      self.p2pPortInput = QtWidgets.QLineEdit()
      self.p2pPortInput.setFixedWidth(100)
      self.p2pPortInput.setText("8333")

      # RPC Port (grayed out placeholder)
      rpcPortLabel = QtWidgets.QLabel(self.tr("RPC Port"))
      self.rpcPortInput = QtWidgets.QLineEdit()
      self.rpcPortInput.setFixedWidth(100)
      self.rpcPortInput.setEnabled(False)
      self.rpcPortInput.setToolTip("Standard Bitcoin Core RPC port (not configurable in Armory)")

      # Add widgets to grid layout
      grid.addWidget(operationLabel, 0, 0)
      grid.addWidget(self.scenarioCombo, 0, 1)
      grid.addWidget(networkLabel, 1, 0)
      grid.addWidget(self.networkModeCombo, 1, 1)
      grid.addWidget(p2pPortLabel, 2, 0)
      grid.addWidget(self.p2pPortInput, 2, 1)
      grid.addWidget(rpcPortLabel, 3, 0)
      grid.addWidget(self.rpcPortInput, 3, 1)
      grid.setColumnStretch(1, 1)

      coreFrameLayout.addLayout(grid)

      return coreFrame

   def scenarioChanged(self, index):
      """Handle changes to the scenario selection."""
      if index == 0 or index == 1:
         # Ports are managed by the backend/bridge; keep inputs disabled
         self.p2pPortInput.setEnabled(False)
         self.rpcPortInput.setEnabled(False)

   def showEvent(self, event):
      """Handle show event to ensure the dialog is properly displayed."""
      super().showEvent(event)
      # Make sure the dialog is visible and active
      self.setWindowState(
         self.windowState() & ~QtCore.Qt.WindowMinimized
         | QtCore.Qt.WindowActive
      )
      self.activateWindow()
      self.raise_()

   @QtCore.Slot()
   def onBridgeReady(self):
      # Called by bridge-ready callback; now populate list and enable refresh
      try:
         if self.refreshButton:
            self.refreshButton.setEnabled(True)
      except Exception:
         pass
      self.loadWalletList()

   def browseSatoshiHome(self):
      """Open a directory dialog to select the Bitcoin Core data directory."""
      dir = QtWidgets.QFileDialog.getExistingDirectory(
         self,
         self.tr('Select Bitcoin Core Data Directory'),
         os.path.expanduser('~')
      )
      if dir:
         self.satoshiHomePath.setText(dir)

   def browseDirDialog(self, lineEdit):
      """Open directory chooser into the given QLineEdit using shared helper."""
      from qtdialogs.qtdefines import selectDirectoryForQLineEdit
      selectDirectoryForQLineEdit(self, lineEdit, title=self.tr('Select Directory'))

   def networkModeChanged(self, index):
      """Handle changes to the network mode selection."""
      # Update P2P port based on network mode
      if index == 0:  # Mainnet
         self.p2pPortInput.setText("8333")
         self.rpcPortInput.setText("")  # Clear RPC port
      elif index == 1:  # Testnet
         self.p2pPortInput.setText("18333")
         self.rpcPortInput.setText("")  # Clear RPC port
      else:  # Regtest
         self.p2pPortInput.setText("18444")
         self.rpcPortInput.setText("")  # Clear RPC port

   def createWalletTab(self):
      """Create the wallet settings tab with directory input and wallet list."""
      tab = QtWidgets.QWidget()
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      # Header
      title = QRichLabel(self.tr(
         '<span style="font-size:14pt;"><b>Wallet Setup</b></span>'))
      title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
      mainLayout.addWidget(title)
      # No decorative line under header

      # Directory Settings Frame
      dirFrame = QtWidgets.QGroupBox(self.tr('Armory Data'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      # Create grid layout for directory settings
      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      # Armory Data Directory
      dirLabel = QtWidgets.QLabel(self.tr("Armory Data Directory"))
      dirLabel.setToolTip('The directory where Armory will store all wallet '
         'files and settings.\n\nThis directory should be:\n• On a secure '
         'drive\n• Regularly backed up\n• Have sufficient free space')

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      self.armoryDataDirEdit = QtWidgets.QLineEdit()
      self.armoryDataDirEdit.setMinimumWidth(400)

      browseBtn = QtWidgets.QPushButton(self.tr("Browse..."))
      browseBtn.setFixedWidth(100)  # Match Accept/Cancel buttons
      browseBtn.clicked.connect(lambda: self.browseDirDialog(
         self.armoryDataDirEdit))
      browseBtn.setToolTip('Click to browse for a directory to store Armory '
         'data.\n\nChoose a location that is:\n• Secure and private\n• Has '
         'sufficient space\n• Is on a reliable drive')

      dirInputLayout.addWidget(self.armoryDataDirEdit)
      dirInputLayout.addWidget(browseBtn)

      # Add directory settings to grid
      dirGrid.addWidget(dirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      # Add grid to directory frame
      dirFrameLayout.addLayout(dirGrid)

      # Add directory frame to main layout
      mainLayout.addWidget(dirFrame)

      # Wallet List Frame
      walletFrame = QtWidgets.QGroupBox(self.tr('Available Wallets'))
      walletFrameLayout = QtWidgets.QVBoxLayout(walletFrame)
      walletFrameLayout.setContentsMargins(12, 12, 12, 12)
      walletFrameLayout.setSpacing(8)

      # Create grid layout for wallet list settings
      walletGrid = QtWidgets.QGridLayout()
      walletGrid.setSpacing(8)

      # Wallet List Title
      walletTitle = QtWidgets.QLabel(self.tr("Wallets Found"))
      walletTitle.setToolTip('Displays all Bitcoin wallets found in the '
         'selected directory.\n\nThe list shows:\n• Wallet IDs - Unique '
         'identifiers for each wallet\n• Names - Custom labels for easy '
         'identification\n• Types - Standard, Offline, or Backup\n• Actions '
         '- Load or Decrypt options')

      # Wallet List
      self.walletList = QtWidgets.QTreeWidget()
      self.walletList.setHeaderLabels(['', 'Wallet ID', 'Type', 'Action'])
      self.walletList.setColumnWidth(0, 28)
      self.walletList.setColumnWidth(1, 260)
      self.walletList.setColumnWidth(2, 120)
      self.walletList.setColumnWidth(3, 240)
      self.walletList.setSelectionMode(QtWidgets.QAbstractItemView.NoSelection)
      self.walletList.setIndentation(0)
      self.walletList.setRootIsDecorated(False)
      # Avoid style hacks that can blank the viewport on some Qt builds

      # Add wallet list to grid
      walletGrid.addWidget(walletTitle, 0, 0)
      walletGrid.addWidget(self.walletList, 1, 0)
      walletGrid.setRowStretch(1, 1)

      # Add grid to wallet frame
      walletFrameLayout.addLayout(walletGrid)

      # Refresh button with center alignment
      self.refreshButton = QtWidgets.QPushButton(self.tr('Refresh Wallet List'))
      self.refreshButton.setFixedWidth(200)
      self.refreshButton.clicked.connect(self.loadWalletList)
      self.refreshButton.setEnabled(True)
      # Keep enabled; loadWalletList handles readiness gracefully
      # No footer line
      self.refreshButton.setToolTip('Click to refresh the list of available '
         'wallets.\n\nUse this when you:\n• Add new wallets to the '
         'directory\n• Remove wallets from the directory\n• Want to ensure '
         'the list is up to date')

      # Add button to wallet frame with center alignment
      buttonLayout = QtWidgets.QHBoxLayout()
      buttonLayout.addStretch()
      buttonLayout.addWidget(self.refreshButton)
      buttonLayout.addStretch()
      walletFrameLayout.addLayout(buttonLayout)

      # Add wallet frame to main layout
      mainLayout.addWidget(walletFrame)
      mainLayout.addStretch()

      tab.setLayout(mainLayout)
      return tab

   def loadWalletList(self):
      """Query bridge for wallets and render actions per type/state."""
      self.walletList.clear()
      self.walletIdToCheckbox.clear()
      try:
         addedAny = False
         # Single bridge query; if it raises, show placeholder and return
         try:
            wltList = TheBridge.wltManager.listWallets()
         except Exception as e:
            addPlaceholderRow(self.walletList, ['', self.tr('Bridge not ready'), '', ''])
            return

         # Iterate directly; capnp lists are iterable
         for entry in wltList:
            if self._createWalletRow(entry):
               addedAny = True

         # If still nothing, show guidance
         if not addedAny:
            addPlaceholderRow(self.walletList, [
               self.tr(''),
               self.tr('No wallets found.'),
               self.tr(''),
               self.tr('')
            ])

      except Exception as e:
         LOGEXCEPT('Error loading wallet list: %s', str(e))

   def _createWalletRow(self, entry):
      """Create and add a wallet row from a bridge entry.

      Returns True if the row is rendered successfully; False otherwise.
      """
      try:
         walletPath = entry.path
         fileName = os.path.basename(walletPath) if walletPath else ''
         walletId = entry.walletId
         # Create row
         item = QtWidgets.QTreeWidgetItem()
         # Use walletId as display name (wallet name); fallback to filename if absent
         displayName = walletId if walletId else os.path.splitext(fileName)[0]
         item.setText(1, displayName)
         # Determine state label via helper
         stateText, isLegacy, isEncrypted = self._walletStateLabel(
            entry, walletId)
         item.setText(2, stateText)

         # Add the row before attaching widgets
         self.walletList.addTopLevelItem(item)

         # Column 0: selector checkbox (only for loadable wallets)
         if isLegacy or isEncrypted or not walletId:
            cell, _ = makeCheckboxCell(False, False)
            self.walletList.setItemWidget(item, 0, cell)
         else:
            cell, cb = makeCheckboxCell(True, True, lambda state,
               wid=walletId: self.onStageCheckboxChanged(
               wid, state == QtCore.Qt.Checked))
            
            self.walletList.setItemWidget(item, 0, cell)
            self.walletIdToCheckbox[walletId] = cb

         fullPath = walletPath if walletPath else os.path.join(self.armoryDataDirEdit.text(), fileName)

         # Persist metadata on the row for later handlers
         stagedFlag = bool(entry.staged)
         item.setData(0, QtCore.Qt.UserRole, {
            'walletId': walletId,
            'path': fullPath,
            'staged': stagedFlag
         })

         # Column 3: status (Ready/Unlock/Migrate)
         if isLegacy:
            cell, _ = makeButtonCell(
               self.tr('Migrate'),
               lambda _, p=fullPath: self.migrateWallet(p)
            )
            self.walletList.setItemWidget(item, 3, cell)
         elif isEncrypted:
            cell, _ = makeButtonCell(
               self.tr('Unlock'),
               lambda _, wid=walletId: self.unlockWallet(wid)
            )
            self.walletList.setItemWidget(item, 3, cell)
         else:
            self.walletList.setItemWidget(item, 3, QtWidgets.QLabel(self.tr('Ready')))

         return True
      except Exception as e:
         LOGEXCEPT('Error creating wallet row: %s', str(e))
         return False

   def _walletStateLabel(self, entry, walletId):
      """Map bridge state to UI label and flags.

      Returns (label, isLegacy, isEncrypted). Avoids per-row imports by using
      the bridge-provided enum: unknown(0), legacy(1), migrated(2), encrypted(3),
      ready(4), loaded(5).
      """
      label = self.tr('Ready')
      isLegacy = False
      isEncrypted = False

      try:
         stateVal = entry.state
         # Handle numeric enums and string fallbacks defensively
         if isinstance(stateVal, int):
            if stateVal == 1:
               label = self.tr('Legacy')
               isLegacy = True
            elif stateVal == 3:
               label = self.tr('Encrypted')
               isEncrypted = True
            elif stateVal == 5:
               # treat loaded as ready for this setup view
               label = self.tr('Ready')
            elif stateVal in (2, 4):
               label = self.tr('Ready')
            elif stateVal == 0:
               label = self.tr('Unknown')
         else:
            s = str(stateVal).lower()
            if s == 'legacy':
               label = self.tr('Legacy')
               isLegacy = True
            elif s == 'encrypted':
               label = self.tr('Encrypted')
               isEncrypted = True
            elif s == 'loaded':
               label = self.tr('Ready')
            elif s in ('migrated', 'ready'):
               label = self.tr('Ready')
            elif s == 'unknown':
               label = self.tr('Unknown')
      except Exception:
         # Default to Ready on any unexpected schema
         label = self.tr('Ready')
         isLegacy = False
         isEncrypted = False

      return (label, isLegacy, isEncrypted)

   def migrateWallet(self, walletPath):
      """Start migration for a legacy wallet.

      - If a valid main window is available, use the full migration dialog.
      - Otherwise, fall back to bridge-driven migration with a simple callback.
      """
      try:
         if self.main is not None:
            walletData = TheBridge.utils.importWallet(walletPath)
            dlg = DlgWalletMigration(self, self.main, walletPath, walletData)
            dlg.exec_()
            return

         # Fallback: run migration directly via the bridge (no wizard UI)
         import uuid
         callbackId = str(uuid.uuid4())

         def migrationCallback(success, result):
            TheSignalExecution.executeMethod(
               self.onMigrationComplete, success, str(result))

         TheBridge.wltManager.migrateWallet(walletPath, callbackId, migrationCallback)
         QtWidgets.QMessageBox.information(
            self,
            self.tr('Migration Started'),
            self.tr('Wallet migration has started. Please wait...')
         )
      except Exception as e:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Migration Failed'),
            self.tr('Failed to start migration: {}').format(str(e))
         )

   @QtCore.Slot(bool, str)
   def onMigrationComplete(self, success, result):
      """Handle migration completion callback and inform the user."""
      if success:
         QtWidgets.QMessageBox.information(
            self,
            self.tr('Migration Complete'),
            self.tr('Wallet migration completed successfully.')
         )
      else:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Migration Failed'),
            self.tr('Migration failed: {}').format(result)
         )

   def unlockWallet(self, walletId):
      """Unlock an encrypted wallet's public data when prompted by backend."""
      try:
         # Use row metadata instead of refetching from backend
         pathForId = None
         rowMeta = None
         for i in range(self.walletList.topLevelItemCount()):
            itm = self.walletList.topLevelItem(i)
            meta = itm.data(0, QtCore.Qt.UserRole)
            if meta and meta.get('walletId') == walletId:
               pathForId = meta.get('path')
               rowMeta = meta
               break
         if not pathForId:
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Unlock Failed'),
               self.tr('Wallet not found.')
            )
            return

         # If we have a walletId, temporarily stage it (if not already), trigger
         # a regular load to let the backend push an unlockRequest, and display the
         # dialog to catch that prompt.
         originallyStaged = bool(rowMeta.get('staged')) if rowMeta else False
         try:
            if walletId and not originallyStaged:
               TheBridge.wltManager.stageWallet(walletId, True)

            # Prepare the dialog first so it can receive the callback
            dlg = UnlockWalletHandler(walletId, self.tr('Unlock Wallet'), self)

            # Kick off backend load asynchronously so the prompt arrives while
            # dialog is up
            QtCore.QTimer.singleShot(
               0, lambda: TheBridge.wltManager.loadWallets())

            dlg.exec_()
         finally:
            # Restore original staging if we changed it
            try:
               if walletId and not originallyStaged:
                  TheBridge.wltManager.stageWallet(walletId, False)
            except Exception:
               pass

         # rely on backend state propagation; no explicit refresh here
      except Exception as e:
         QtWidgets.QMessageBox.warning(self, self.tr('Unlock Failed'), str(e))

   def stageWallet(self, walletId):
      try:
         # Stage only; bulk loading is handled elsewhere
         TheBridge.wltManager.stageWallet(walletId, True)
      except Exception as e:
         QtWidgets.QMessageBox.warning(self, self.tr('Stage Failed'), str(e))

   def onStageCheckboxChanged(self, walletId, checked):
      try:
         TheBridge.wltManager.stageWallet(walletId, checked)
      except Exception as e:
         cb = self.walletIdToCheckbox.get(walletId)
         if cb:
            cb.blockSignals(True)
            cb.setChecked(not checked)
            cb.blockSignals(False)
         QtWidgets.QMessageBox.warning(self, self.tr('Stage Failed'), str(e))

   def createDatabaseTab(self):
      tab = QtWidgets.QWidget()
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      # Header
      title = QRichLabel(self.tr('<span style="font-size:14pt;"><b>Database Settings</b></span>'))
      title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
      mainLayout.addWidget(title)
      # No decorative line under header

      # Directory Settings Frame
      dirFrame = QtWidgets.QGroupBox(self.tr('Database Directory'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      # Database Directory
      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      dbDirLabel = QtWidgets.QLabel(self.tr("Location"))

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      self.databaseDirEdit = QtWidgets.QLineEdit()
      self.databaseDirEdit.setMinimumWidth(400)

      dbDirButton = QtWidgets.QPushButton(self.tr("Browse..."))
      dbDirButton.setFixedWidth(100)
      dbDirButton.clicked.connect(lambda: self.browseDirDialog(self.databaseDirEdit))

      dirInputLayout.addWidget(self.databaseDirEdit)
      dirInputLayout.addWidget(dbDirButton)

      dirGrid.addWidget(dbDirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      dirFrameLayout.addLayout(dirGrid)
      mainLayout.addWidget(dirFrame)

      # Scenario Selection Frame
      scenarioFrame = QtWidgets.QGroupBox(self.tr('Database Scenario'))
      scenarioLayout = QtWidgets.QVBoxLayout(scenarioFrame)
      scenarioLayout.setContentsMargins(12, 12, 12, 12)
      scenarioLayout.setSpacing(8)

      scenarioGrid = QtWidgets.QGridLayout()
      scenarioGrid.setSpacing(8)

      # Database Scenario
      dbScenarioLabel = QtWidgets.QLabel(self.tr("Mode"))
      self.databaseScenarioCombo = QtWidgets.QComboBox()
      self.databaseScenarioCombo.setFixedWidth(200)
      self.databaseScenarioCombo.addItems(["Local Database", "Remote Database", "No Database"])
      self.databaseScenarioCombo.currentIndexChanged.connect(self.handleDatabaseScenarioChange)

      scenarioGrid.addWidget(dbScenarioLabel, 0, 0)
      scenarioGrid.addWidget(self.databaseScenarioCombo, 0, 1)
      scenarioGrid.setColumnStretch(1, 1)

      scenarioLayout.addLayout(scenarioGrid)
      mainLayout.addWidget(scenarioFrame)

      # Local Database Settings Frame
      self.localDatabaseFrame = QtWidgets.QGroupBox(self.tr('Local Database Settings'))
      localDbLayout = QtWidgets.QVBoxLayout(self.localDatabaseFrame)
      localDbLayout.setContentsMargins(12, 12, 12, 12)
      localDbLayout.setSpacing(8)

      localDbGrid = QtWidgets.QGridLayout()
      localDbGrid.setSpacing(8)

      # Database Type
      dbTypeLabel = QtWidgets.QLabel(self.tr("Database Type"))
      self.databaseTypeCombo = QtWidgets.QComboBox()
      self.databaseTypeCombo.setFixedWidth(200)
      self.databaseTypeCombo.addItems(["Full Database", "Supernode"])

      # RAM Usage
      ramLabel = QtWidgets.QLabel(self.tr("RAM Usage (MB)"))
      self.ramUsageEdit = QtWidgets.QLineEdit()
      self.ramUsageEdit.setFixedWidth(100)

      # Thread Count
      threadLabel = QtWidgets.QLabel(self.tr("Thread Count"))
      self.threadCountEdit = QtWidgets.QLineEdit()
      self.threadCountEdit.setFixedWidth(100)

      localDbGrid.addWidget(dbTypeLabel, 0, 0)
      localDbGrid.addWidget(self.databaseTypeCombo, 0, 1)
      localDbGrid.addWidget(ramLabel, 1, 0)
      localDbGrid.addWidget(self.ramUsageEdit, 1, 1)
      localDbGrid.addWidget(threadLabel, 2, 0)
      localDbGrid.addWidget(self.threadCountEdit, 2, 1)
      localDbGrid.setColumnStretch(1, 1)

      localDbLayout.addLayout(localDbGrid)
      mainLayout.addWidget(self.localDatabaseFrame)

      # Remote Connection Frame
      self.remoteFrame = QtWidgets.QGroupBox(self.tr('Remote Connection'))
      remoteLayout = QtWidgets.QVBoxLayout(self.remoteFrame)
      remoteLayout.setContentsMargins(12, 12, 12, 12)
      remoteLayout.setSpacing(8)

      remoteGrid = QtWidgets.QGridLayout()
      remoteGrid.setSpacing(8)

      # Remote Host
      hostLabel = QtWidgets.QLabel(self.tr("Remote Host"))
      self.remoteHostEdit = QtWidgets.QLineEdit()
      self.remoteHostEdit.setFixedWidth(200)

      # Remote Port
      portLabel = QtWidgets.QLabel(self.tr("Remote Port"))
      self.remotePortEdit = QtWidgets.QLineEdit()
      self.remotePortEdit.setFixedWidth(100)

      # Remote Username
      userLabel = QtWidgets.QLabel(self.tr("Username"))
      self.remoteUserEdit = QtWidgets.QLineEdit()
      self.remoteUserEdit.setFixedWidth(200)

      # Remote Password
      passLabel = QtWidgets.QLabel(self.tr("Password"))
      self.remotePasswordEdit = QtWidgets.QLineEdit()
      self.remotePasswordEdit.setFixedWidth(200)
      self.remotePasswordEdit.setEchoMode(QtWidgets.QLineEdit.Password)

      remoteGrid.addWidget(hostLabel, 0, 0)
      remoteGrid.addWidget(self.remoteHostEdit, 0, 1)
      remoteGrid.addWidget(portLabel, 1, 0)
      remoteGrid.addWidget(self.remotePortEdit, 1, 1)
      remoteGrid.addWidget(userLabel, 2, 0)
      remoteGrid.addWidget(self.remoteUserEdit, 2, 1)
      remoteGrid.addWidget(passLabel, 3, 0)
      remoteGrid.addWidget(self.remotePasswordEdit, 3, 1)
      remoteGrid.setColumnStretch(1, 1)

      remoteLayout.addLayout(remoteGrid)

      # Test Connection Button
      self.testConnectionButton = QtWidgets.QPushButton(self.tr("Test Connection"))
      self.testConnectionButton.setFixedWidth(200)
      self.testConnectionButton.clicked.connect(self.testRemoteConnection)

      buttonLayout = QtWidgets.QHBoxLayout()
      buttonLayout.addStretch()
      buttonLayout.addWidget(self.testConnectionButton)
      buttonLayout.addStretch()
      remoteLayout.addLayout(buttonLayout)

      mainLayout.addWidget(self.remoteFrame)
      mainLayout.addStretch()

      # Initially hide only the remote frame, show local DB frame by default
      self.remoteFrame.hide()

      tab.setLayout(mainLayout)
      return tab

   def handleDatabaseScenarioChange(self, index):
      """Handle changes to the database scenario selection."""
      isLocal = index == 0
      isRemote = index == 1
      isNoDb = index == 2

      # Show/hide frames based on scenario
      self.localDatabaseFrame.setVisible(isLocal)
      self.remoteFrame.setVisible(isRemote)

      # Enable/disable directory input based on scenario
      self.databaseDirEdit.setEnabled(isLocal)
      self.databaseDirEdit.parentWidget().setEnabled(True)  # Always keep directory frame visible

   def testRemoteConnection(self):
      """Delegate connection setup to the bridge; no local management."""
      try:
         TheBridge.service.setupDB()
         QtWidgets.QMessageBox.information(
            self,
            self.tr('Connection Test'),
            self.tr('Requested bridge to (re)connect to the database. Check main status.')
         )
      except Exception as e:
         LOGEXCEPT('DB setup request failed: %s', str(e))
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Connection Error'),
            self.tr('Failed to request DB setup via bridge: {}').format(str(e))
         )

   def loadSettings(self, testing=False):
      """Load settings from configuration including directory paths and database settings.

      Args:
          testing: Whether this is being used for testing (uses empty defaults if True)
      """
      # Seed directory paths from engine/bridge (source of truth), not from TheSettings
      try:
         btcDir = TheBDM.btcdir
      except Exception:
         btcDir = BTC_HOME_DIR
      self.satoshiHomePath.setText(os.path.normpath(btcDir))
      try:
         self.armoryDataDirEdit.setText(os.path.normpath(ARMORY_HOME_DIR))
      except Exception:
         self.armoryDataDirEdit.setText('')
      try:
         self.databaseDirEdit.setText(os.path.normpath(ARMORY_DB_DIR))
      except Exception:
         self.databaseDirEdit.setText('')

      # Determine if we have existing Core settings
      hasCoreSettings = bool(self.satoshiHomePath.text() and os.path.exists(self.satoshiHomePath.text()))
      self.scenarioCombo.setCurrentIndex(0 if hasCoreSettings else 1)

      # Load Core settings
      networkMode = TheSettings.getSettingOrSetDefault('NetworkMode', 'Mainnet')
      self.networkModeCombo.setCurrentText(networkMode)

      # Load database configuration settings
      dbScenario = TheSettings.getSettingOrSetDefault('DBScenario', 'Run Local Database')
      self.databaseScenarioCombo.setCurrentText(dbScenario)

      # Ensure frames reflect the loaded scenario
      self.handleDatabaseScenarioChange(self.databaseScenarioCombo.currentIndex())

      # Hide remote frame by default
      self.remoteFrame.setVisible(False)

      if dbScenario == 'Run Local Database':
         dbTypeSetting = TheSettings.getSettingOrSetDefault('DBType', 'DB_FULL')
         # Map stored value to display label
         if dbTypeSetting == 'DB_SUPER':
            self.databaseTypeCombo.setCurrentText('Supernode')
         else:
            self.databaseTypeCombo.setCurrentText('Full Database')
         self.ramUsageEdit.setText(str(TheSettings.getSettingOrSetDefault('RAMUsage', 50)))
         self.threadCountEdit.setText(str(TheSettings.getSettingOrSetDefault('ThreadCount', 4)))
      elif dbScenario == 'Connect to Remote Database':
         # Do not prefill credentials from settings per security/UX policy
         self.remoteHostEdit.setText('')
         self.remotePortEdit.setText('')
         self.remoteUserEdit.setText('')
         self.remotePasswordEdit.setText('')

   def validateSettings(self, interactive=True):
      """Validate directory paths group and create as needed (interactive)."""
      paths = self._collectPathsGroup()
      corePath = paths['core']
      armoryPath = paths['armory']
      dbPath = paths['db']

      # For non-interactive validation (unit tests), just check if paths exist
      if not interactive:
         return os.path.exists(corePath) and os.path.exists(armoryPath) and os.path.exists(dbPath)

      # Interactive validation with directory creation dialogs
      if not os.path.exists(corePath):
         reply = QtWidgets.QMessageBox.question(
            self,
            self.tr('Create Directory?'),
            self.tr('Bitcoin Core data directory does not exist. Would you like to create it?'),
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No | QtWidgets.QMessageBox.Cancel
         )

         if reply == QtWidgets.QMessageBox.Yes:
            try:
               os.makedirs(corePath)
            except Exception as e:
               QtWidgets.QMessageBox.critical(
                  self,
                  self.tr('Error'),
                  self.tr('Could not create Bitcoin Core data directory: {}').format(str(e))
               )
               return False
         elif reply == QtWidgets.QMessageBox.No:
            # Let user select a different directory
            newDir = QtWidgets.QFileDialog.getExistingDirectory(
               self,
               self.tr('Select Bitcoin Core Data Directory'),
               os.path.expanduser('~')
            )
            if newDir:
               self.satoshiHomePath.setText(newDir)
               return self.validateSettings()  # Recursive check with new directory
            return False
         else:  # Cancel
            return False

      # Create Armory directory if it doesn't exist
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

      # Create database directory if it doesn't exist
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
      # Paths (interactive create flow)
      if not self.validateSettings(interactive=True):
         return False

      # Core group
      if not self._validateCoreGroup():
         return False

      # Wallets group (soft check; warn if bridge down)
      if not self._validateWalletsGroup():
         return False

      # DB group
      if not self._validateDbGroup():
         return False

      return True

   def _validateCoreGroup(self):
      try:
         scenarioOk = self.scenarioCombo.currentIndex() in (0, 1)
         modeOk = self.networkModeCombo.currentText() in ('Mainnet', 'Testnet', 'Regtest')
         if not (scenarioOk and modeOk):
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Invalid Core Settings'),
               self.tr('Please select a valid scenario and network mode.')
            )
            return False
         # Keep inputs disabled regardless
         self.p2pPortInput.setEnabled(False)
         self.rpcPortInput.setEnabled(False)
         return True
      except Exception:
         return False

   def _validateWalletsGroup(self):
      try:
         # Do not probe bridge readiness here; UI handles it during listing
         return True
      except Exception:
         return True

   def _validateDbGroup(self):
      try:
         db = self._collectDbGroup()
         if db['scenario'] == 'Connect to Remote Database':
            host = db['remoteHost']
            portText = db['remotePort']
            if not host or not portText:
                QtWidgets.QMessageBox.warning(
                   self,
                   self.tr('Missing Remote DB Info'),
                   self.tr('Please provide host and port for the remote database.')
                )
                return False
            try:
               port = int(portText)
               if port < 1 or port > 65535:
                  raise ValueError()
            except ValueError:
               QtWidgets.QMessageBox.warning(
                  self,
                  self.tr('Invalid Port'),
                  self.tr('Remote DB port must be an integer between 1 and 65535.')
               )
               return False
         return True
      except Exception:
         return False

   # ----------------------
   # Feature groups helpers
   # ----------------------
   def _collectPathsGroup(self):
      """Return current paths from UI as a dict."""
      return {
         'core': self.satoshiHomePath.text(),
         'armory': self.armoryDataDirEdit.text(),
         'db': self.databaseDirEdit.text(),
      }

   def _collectCoreGroup(self):
      """Return current core settings from UI as a dict."""
      return {
         'networkMode': self.networkModeCombo.currentText(),
         'manageSatoshi': (self.scenarioCombo.currentIndex() == 0),
      }

   def _collectDbGroup(self):
      """Return current database config from UI as a dict."""
      dbScenario = self.databaseScenarioCombo.currentText()
      dbTypeDisp = self.databaseTypeCombo.currentText() if dbScenario == 'Run Local Database' else ''
      return {
         'scenario': dbScenario,
         'typeDisp': dbTypeDisp,
         'remoteHost': self.remoteHostEdit.text() if dbScenario == 'Connect to Remote Database' else '',
         'remotePort': self.remotePortEdit.text() if dbScenario == 'Connect to Remote Database' else '',
         'remoteUser': self.remoteUserEdit.text() if dbScenario == 'Connect to Remote Database' else '',
         'remotePass': self.remotePasswordEdit.text() if dbScenario == 'Connect to Remote Database' else '',
         'ram': self.ramUsageEdit.text() if dbScenario == 'Run Local Database' else '',
         'threads': self.threadCountEdit.text() if dbScenario == 'Run Local Database' else '',
      }

   def _applySettingsGroups(self, paths, core, db):
      """Persist settings and propagate runtime options based on grouped dicts."""
      try:
         # Paths
         TheSettings.set('CoreDataDir', paths['core'])
         TheSettings.set('ArmoryDataDir', paths['armory'])
         TheSettings.set('DBDir', paths['db'])

         # Core
         TheSettings.set('SatoshiDatadir', paths['core'])
         TheSettings.set('ManageSatoshi', core['manageSatoshi'])
         TheSettings.set('NetworkMode', core['networkMode'])

         # DB scenario
         TheSettings.set('DBScenario', db['scenario'])
         if db['scenario'] == 'Run Local Database':
            dbTypeVal = 'DB_SUPER' if db['typeDisp'] == 'Supernode' else 'DB_FULL'
            TheSettings.set('DBType', dbTypeVal)
            if db['ram']:
               TheSettings.set('RAMUsage', int(db['ram']))
            if db['threads']:
               TheSettings.set('ThreadCount', int(db['threads']))
         elif db['scenario'] == 'Connect to Remote Database':
            # Do not persist credentials in settings; bridge will manage runtime
            pass

         # Notify main (best-effort)
         try:
            # Main window implements setSatoshiPaths; call best-effort
            self.main.setSatoshiPaths()
         except Exception:
            pass
      except Exception:
         pass

   def saveSettings(self):
      """Save directory paths, database settings, and update bridge arguments in the configuration."""
      paths = self._collectPathsGroup()
      core = self._collectCoreGroup()
      db = self._collectDbGroup()
      self._applySettingsGroups(paths, core, db)

   @staticmethod
   def run(parent=None, main=None, testing=False):
      """Optional helper for callers that want to show the dialog.

      Note: This function is intentionally simple and non-invasive. It does
      not start the bridge, show splash screens, or manage the main window.
      """
      dlg = DlgSetupManager(parent=parent, main=main, testing=testing)
      return dlg.exec_()
