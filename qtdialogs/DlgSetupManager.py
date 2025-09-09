##############################################################################
#                                                                            #
#  Copyright (C) 2025, goatpig                                               #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

import os
from io import BytesIO
from struct import pack
from types import MethodType
from qtpy import QtCore, QtWidgets, QtGui
from armoryengine.ArmoryUtils import BTC_HOME_DIR, ARMORY_DB_DIR, \
   ARMORY_HOME_DIR, CLI_OPTIONS, LOGEXCEPT, LOGINFO, RightNow
from armoryengine.Settings import TheSettings
from armoryengine.CppBridge import TheBridge
from armoryengine.BDM import TheBDM
from armoryengine.WalletUtils import WalletMap
from ui.QtExecuteSignal import TheSignalExecution

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgWalletMigration import DlgWalletMigration
from qtdialogs.qtdefines import QRichLabel, applyDialogBaseStyle, \
   makeCenteredCell, makeCheckboxCell, makeButtonCell, addPlaceholderRow, \
   selectDirectoryForQLineEdit

# --- Dialog-specific constants ---
MINIMUM_DIALOG_WIDTH = 300
MINIMUM_DIALOG_HEIGHT = 500

# Wallet state enums (from bridge)
WALLET_STATE_UNKNOWN = 0
WALLET_STATE_LEGACY = 1
WALLET_STATE_MIGRATED = 2
WALLET_STATE_ENCRYPTED = 3
WALLET_STATE_LOADED = 5

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
      # Bridge readiness will be handled by callback from main script

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

      # Callback for spawning main window
      self.mainWindowSpawner = None

      # Create wallet manager for migration functionality
      # This allows DlgWalletMigration to work without requiring
      # ArmoryMainWindow
      self.walletManager = WalletMap(self)

      # Alias for compatibility with DlgWalletMigration expectations
      self.wallets = self.walletManager

      # Entropy accumulator for DlgWalletMigration compatibility
      # This is used by registerWidgetActivateTime for cryptographic randomness
      self.entropyAccum = BytesIO()

      # Widget references for settings
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
      self.bridgeReady = False

   def setupDialogProperties(self):
      """Configure basic dialog properties and styling."""
      self.setMinimumWidth(MINIMUM_DIALOG_WIDTH)
      self.setMinimumHeight(MINIMUM_DIALOG_HEIGHT)
      self.setWindowTitle(self.tr('Armory Setup Manager'))
      self.setWindowFlags(QtCore.Qt.Window)
      self.setModal(True)
      # Use default dialog styling (match DlgWalletMigration look)
      applyDialogBaseStyle(self)

   def initTabs(self):
      """Initialize all tab widgets."""
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

      # Wallet tab signals (auto-refresh on bridge ready)

      # Database tab signals
      if self.databaseScenarioCombo:
         self.databaseScenarioCombo.currentIndexChanged.connect(
            self.handleDatabaseScenarioChange)
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

         # Close dialog
         super(DlgSetupManager, self).accept()

         # Spawn main window if spawner callback is provided
         if self.mainWindowSpawner and callable(self.mainWindowSpawner):
            self.mainWindowSpawner()

      except Exception as e:
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Error'),
            self.tr('Failed to save settings: {}').format(str(e))
         )

   def reject(self):
      """Handle dialog rejection without marking setup as completed."""
      super(DlgSetupManager, self).reject()

   def createCoreTab(self):
      """Create the Core settings tab with directory and network config."""
      tab = QtWidgets.QWidget()
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      # Header
      title = QRichLabel(
         self.tr('<span style="font-size:14pt;"><b>Core Settings</b></span>'))
      title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
      mainLayout.addWidget(title)
      # Remove decorative horizontal lines for a cleaner look

      # Directory Settings Frame
      dirFrame = self.createDirectoryFrame()
      mainLayout.addWidget(dirFrame)

      # Core Settings Frame  
      coreFrame = self.createCoreSettingsFrame()
      mainLayout.addWidget(coreFrame)

      mainLayout.addStretch()
      tab.setLayout(mainLayout)

      # core tab created
      return tab

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
      """Create core settings frame with operation mode and network options."""
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

      # Network Mode (read-only - set by CLI args only)
      networkLabel = QtWidgets.QLabel(self.tr("Network Mode"))
      self.networkModeCombo = QtWidgets.QComboBox()
      self.networkModeCombo.setFixedWidth(200)
      self.networkModeCombo.setEnabled(False)  # CLI args only
      self.networkModeCombo.addItem(self.tr("Mainnet"))
      self.networkModeCombo.addItem(self.tr("Testnet"))
      self.networkModeCombo.addItem(self.tr("Regtest"))

      # Add explanatory label
      networkInfo = QtWidgets.QLabel(self.tr("(Set by command line arguments)"))
      networkInfo.setStyleSheet("color: gray; font-style: italic;")

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
      self.rpcPortInput.setToolTip(
         "Standard Bitcoin Core RPC port (not configurable in Armory)")

      # Add widgets to grid layout
      grid.addWidget(operationLabel, 0, 0)
      grid.addWidget(self.scenarioCombo, 0, 1)
      grid.addWidget(networkLabel, 1, 0)
      grid.addWidget(self.networkModeCombo, 1, 1)
      grid.addWidget(networkInfo, 1, 2)  # Add info label
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
      # Called by bridge-ready callback; now populate list
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
      selectDirectoryForQLineEdit(
         self, lineEdit, title=self.tr('Select Directory'))

   def browseArmoryDataDir(self):
      """Browse for Armory data directory."""
      self.browseDirDialog(self.armoryDataDirEdit)

   def browseDatabaseDir(self):
      """Browse for database directory."""
      self.browseDirDialog(self.databaseDirEdit)

   def networkModeChanged(self, index):
      """Handle changes to the network mode selection."""
      # NOTE: Network mode is set by CLI args only and cannot be changed in UI
      # This method is kept for compatibility but combo is now disabled
      pass

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
      browseBtn.clicked.connect(self.browseArmoryDataDir)
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

      # Wallet List
      self.walletList = QtWidgets.QTreeWidget()
      self.walletList.setHeaderLabels(
         ['', 'Wallet ID', 'File Name', 'Type', 'Action'])
      self.walletList.setColumnWidth(0, 30)   # Checkbox column
      self.walletList.setColumnWidth(1, 120)  # Wallet ID - reduced more
      self.walletList.setColumnWidth(2, 200)  # File Name - wider for full names
      self.walletList.setColumnWidth(3, 100)   # Type column
      self.walletList.setColumnWidth(4, 40)   # Action column - narrower
      self.walletList.setSelectionMode(QtWidgets.QAbstractItemView.NoSelection)
      self.walletList.setIndentation(0)
      self.walletList.setRootIsDecorated(False)
      # Improve header alignment
      header = self.walletList.header()
      header.setDefaultAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
      # Set reasonable size constraints for better proportions
      self.walletList.setMinimumHeight(100)
      self.walletList.setMaximumHeight(200)
      # Avoid style hacks that can blank the viewport on some Qt builds

      # Add wallet list to grid (no title needed)
      walletGrid.addWidget(self.walletList, 0, 0)
      # Remove row stretch to prevent excessive expansion
      walletGrid.setRowStretch(0, 0)

      # Add grid to wallet frame
      walletFrameLayout.addLayout(walletGrid)

      # Wallet list auto-refreshes when bridge is ready

      # Add wallet frame to main layout
      mainLayout.addWidget(walletFrame)
      mainLayout.addStretch()

      tab.setLayout(mainLayout)
      return tab

   def refreshWalletList(self):
      """Refresh wallet list display - required by WalletMap for migration."""
      # This method is called by WalletMap.migrateWallet after migration
      # CRITICAL: May be called from bridge thread, use TheSignalExecution
      # to ensure UI updates happen in the main thread
      TheSignalExecution.executeMethod(self.loadWalletList)

   def getWalletManager(self):
      """Return the wallet manager for transfer to main window."""
      return self.walletManager

   def logEntropy(self):
      """Log entropy for cryptographic randomness - required by migration."""
      self.entropyAccum.write(pack('d', RightNow()))
      self.entropyAccum.write(pack('i', QtGui.QCursor.pos().x()))
      self.entropyAccum.write(pack('i', QtGui.QCursor.pos().y()))

   def registerWidgetActivateTime(self, widget):
      """Register widget for entropy accumulation - required by migration."""
      # This patches widget event handlers to log entropy from user interactions
      # Required for DlgWalletMigration's SetPassphraseFrame
      setupManager = self

      def newKPE(wself, event=None):
         setupManager.logEntropy()
         super(wself.__class__, wself).keyPressEvent(event)

      def newKRE(wself, event=None):
         setupManager.logEntropy()
         super(wself.__class__, wself).keyReleaseEvent(event)

      def newMPE(wself, event=None):
         setupManager.logEntropy()
         super(wself.__class__, wself).mousePressEvent(event)

      def newMRE(wself, event=None):
         setupManager.logEntropy()
         super(wself.__class__, wself).mouseReleaseEvent(event)

      widget.keyPressEvent     = MethodType(newKPE, widget)
      widget.keyReleaseEvent   = MethodType(newKRE, widget)
      widget.mousePressEvent   = MethodType(newMPE, widget)
      widget.mouseReleaseEvent = MethodType(newMRE, widget)

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
            addPlaceholderRow(
               self.walletList, ['', self.tr('Bridge not ready'), '', ''])
            return

         # Pre-process to find migrated versions
         migratedWalletIds = set()
         for entry in wltList:
            try:
               walletId = entry.walletId
               isStaged = entry.staged
               if walletId and isStaged:
                  migratedWalletIds.add(walletId)
            except Exception:
               continue

         # Iterate directly; capnp lists are iterable
         for entry in wltList:
            if self._createWalletRow(entry, migratedWalletIds):
               addedAny = True

         # If still nothing, show guidance
         if not addedAny:
            addPlaceholderRow(self.walletList, [
               self.tr(''),
               self.tr('No wallets found.'),
               self.tr(''),
               self.tr('')
            ])

      except Exception:
         # If wallet list loading fails completely, let it crash
         # The UI will show the placeholder from the inner try/except
         raise

   def _addActionButton(self, item, text, handler):
      """Create and add a left-aligned action button to the wallet list item."""
      btn = QtWidgets.QPushButton(text)
      btn.clicked.connect(handler)
      btn.setMaximumWidth(60)  # Compact button
      cellWidget = QtWidgets.QWidget()
      layout = QtWidgets.QHBoxLayout(cellWidget)
      layout.setContentsMargins(2, 2, 2, 2)  # Small margins
      layout.addWidget(btn)
      layout.addStretch()  # Push button to left
      self.walletList.setItemWidget(item, 4, cellWidget)

   def _createWalletRow(self, entry, migratedWalletIds=None):
      """Create and add a wallet row from a bridge entry.

      Returns True if the row is rendered successfully; False otherwise.
      """
      try:
         walletPath = entry.path
         fileName = os.path.basename(walletPath) if walletPath else ''
         walletId = entry.walletId

         # Check if this is a legacy wallet that has been migrated
         # Per BridgeTests.cpp: after migration, there are 2 wallets with
         # same walletId:
         # - Legacy: staged=false (original file) <- Hide this one
         # - Migrated: staged=true (new file) <- Show this one
         try:
            isStaged = entry.staged
         except Exception:
            isStaged = False

         # Hide legacy wallets ONLY if there's a migrated version
         # Use only staged status since loadState doesn't exist in capnp
         if not isStaged and migratedWalletIds:
            # Check if there's a staged wallet with the same walletId
            # (migrated version)
            if walletId in migratedWalletIds:
               return False  # Skip the legacy version
         # Create row
         item = QtWidgets.QTreeWidgetItem()
         # Use walletId as display name (wallet name); fallback to filename
         # if absent
         displayName = walletId if walletId else os.path.splitext(fileName)[0]
         item.setText(1, displayName)
         # Set file name in column 2 (just the filename, not full path)
         item.setText(2, fileName)
         # Determine state label via helper
         stateText, isLegacy, isEncrypted = self._walletStateLabel(
            entry, walletId)
         item.setText(3, stateText)

         # Add the row before attaching widgets
         self.walletList.addTopLevelItem(item)

         # Column 0: selector checkbox (only for loadable wallets)
         if isLegacy or isEncrypted or not walletId:
            cell, _ = makeCheckboxCell(False, False)
            self.walletList.setItemWidget(item, 0, cell)
         else:
            def stageCheckboxHandler(state):
               self.onStageCheckboxChanged(walletId, state == QtCore.Qt.Checked)
            cell, cb = makeCheckboxCell(True, True, stageCheckboxHandler)

            self.walletList.setItemWidget(item, 0, cell)
            self.walletIdToCheckbox[walletId] = cb

         # Persist metadata on the row for later handlers
         stagedFlag = bool(entry.staged)
         item.setData(0, QtCore.Qt.UserRole, {
            'walletId': walletId,
            'path': walletPath,
            'staged': stagedFlag
         })

         # Column 4: Action buttons (left-aligned for better appearance)
         if isLegacy:
            def migrateHandler(_):
               self.migrateWallet(walletPath)
            self._addActionButton(item, self.tr('Migrate'), migrateHandler)
         elif isEncrypted:
            def unlockHandler(_):
               self.unlockWallet(walletId)
            self._addActionButton(item, self.tr('Unlock'), unlockHandler)
         else:
            # Render nothing in Action column for ready wallets
            pass

         return True
      except Exception:
         return False

   def _walletStateLabel(self, entry, walletId):
      """Map bridge state to UI label and flags.

      Returns (label, isLegacy, isEncrypted). Avoids per-row imports by using
      the bridge-provided enum: unknown(0), legacy(1), migrated(2),
      encrypted(3), ready(4), loaded(5).
      """
      label = self.tr('Ready')
      isLegacy = False
      isEncrypted = False

      try:
         stateVal = entry.state
         # Handle numeric enums and string fallbacks defensively
         if isinstance(stateVal, int):
            if stateVal == WALLET_STATE_LEGACY:
               label = self.tr('Legacy')
               isLegacy = True
            elif stateVal == WALLET_STATE_ENCRYPTED:
               label = self.tr('Encrypted')
               isEncrypted = True
            elif stateVal == WALLET_STATE_LOADED:
               # treat loaded as ready for this setup view
               label = self.tr('Ready')
            elif stateVal in (WALLET_STATE_MIGRATED, 4):
               label = self.tr('Ready')
            elif stateVal == WALLET_STATE_UNKNOWN:
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
      """Start migration using clean dependency injection pattern."""
      try:
         # Import wallet data through bridge
         walletData = TheBridge.utils.importWallet(walletPath)

         # Launch migration dialog with setup manager as main reference
         # DlgSetupManager provides wallet management interface via self.wallets
         dlg = DlgWalletMigration(self, self, walletPath, walletData)
         result = dlg.exec_()

         # Reload wallet list after migration dialog closes
         TheSignalExecution.executeMethod(self.loadWalletList)

         if result == QtWidgets.QDialog.Accepted:
            LOGINFO("Wallet migration completed successfully")
         else:
            LOGINFO("Wallet migration cancelled by user")

      except Exception as e:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Migration Failed'),
            self.tr('Failed to start migration: {}').format(str(e))
         )

   def unlockWallet(self, walletId):
      """Unlock wallet by staging and loading - bridge handles unlock
      prompts."""
      try:
         # Stage the wallet - this triggers unlock prompts via bridge
         # notifications
         # Bridge will automatically handle passphrase prompts and unlock
         # process
         TheBridge.wltManager.stageWallet(walletId, True)

         # Trigger wallet loading - bridge will prompt for unlock if needed
         def loadWalletsAsync():
            TheBridge.wltManager.loadWallets()
            # Refresh UI after load completes
            TheSignalExecution.executeMethod(self.loadWalletList)

         TheSignalExecution.callLater(100, loadWalletsAsync)

      except Exception as e:
         QtWidgets.QMessageBox.warning(
            self, self.tr('Unlock Failed'), str(e))

   def stageWallet(self, walletId, stage=True):
      """Stage/unstage wallet and rely on bridge state propagation."""
      try:
         TheBridge.wltManager.stageWallet(walletId, stage)
         # Bridge state will propagate - no manual refresh needed
      except Exception as e:
         QtWidgets.QMessageBox.warning(self, self.tr('Stage Failed'), str(e))

   def onStageCheckboxChanged(self, walletId, checked):
      """Handle staging checkbox with proper error recovery."""
      try:
         TheBridge.wltManager.stageWallet(walletId, checked)
         # Bridge state propagation will update UI automatically
      except Exception as e:
         # Revert checkbox state on failure
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
      title = QRichLabel(self.tr(
         '<span style="font-size:14pt;"><b>Database Settings</b></span>'))
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
      dbDirButton.clicked.connect(self.browseDatabaseDir)

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
      self.databaseScenarioCombo.addItems([
         "Local Database", "Remote Database", "No Database"])
      self.databaseScenarioCombo.currentIndexChanged.connect(
         self.handleDatabaseScenarioChange)

      scenarioGrid.addWidget(dbScenarioLabel, 0, 0)
      scenarioGrid.addWidget(self.databaseScenarioCombo, 0, 1)
      scenarioGrid.setColumnStretch(1, 1)

      scenarioLayout.addLayout(scenarioGrid)
      mainLayout.addWidget(scenarioFrame)

      # Local Database Settings Frame
      self.localDatabaseFrame = QtWidgets.QGroupBox(
         self.tr('Local Database Settings'))
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

      # Password will be handled by bridge unlock callback
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

      # Test Connection Button
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
      # Always keep directory frame visible
      self.databaseDirEdit.parentWidget().setEnabled(True)

   def testRemoteConnection(self):
      """Test remote database connection without setting up the database."""
      try:
         # Get connection parameters
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

         # Test connection via bridge (without setupDB)
         # Note: Bridge should provide a connection test method separate
         # from setupDB
         QtWidgets.QMessageBox.information(
            self,
            self.tr('Connection Test'),
            self.tr('Connection test functionality needs to be implemented '
               'in bridge.\nHost: {}\nPort: {}').format(host, port)
         )
      except Exception as e:
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Connection Error'),
            self.tr('Failed to test connection: {}').format(str(e))
         )

   def loadSettings(self, testing=False):
      """Load settings from configuration including directory paths and
      database settings.

      Args:
          testing: Whether this is being used for testing (uses empty
            defaults if True)
      """
      # Seed directory paths from engine/bridge (source of truth), not
      # from TheSettings
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
      hasCoreSettings = bool(self.satoshiHomePath.text() and
         os.path.exists(self.satoshiHomePath.text()))
      self.scenarioCombo.setCurrentIndex(0 if hasCoreSettings else 1)

      # Load Core settings
      networkMode = TheSettings.getSettingOrSetDefault('NetworkMode', 'Mainnet')
      self.networkModeCombo.setCurrentText(networkMode)

      # Load database configuration settings
      dbScenario = TheSettings.getSettingOrSetDefault(
         'DBScenario', 'Run Local Database')
      self.databaseScenarioCombo.setCurrentText(dbScenario)

      # Ensure frames reflect the loaded scenario
      self.handleDatabaseScenarioChange(
         self.databaseScenarioCombo.currentIndex())

      # Hide remote frame by default
      self.remoteFrame.setVisible(False)

      if dbScenario == 'Run Local Database':
         dbTypeSetting = TheSettings.getSettingOrSetDefault('DBType', 'DB_FULL')
         # Map stored value to display label
         if dbTypeSetting == 'DB_SUPER':
            self.databaseTypeCombo.setCurrentText('Supernode')
         else:
            self.databaseTypeCombo.setCurrentText('Full Database')
         self.ramUsageEdit.setText(
            str(TheSettings.getSettingOrSetDefault('RAMUsage', 50)))
         self.threadCountEdit.setText(
            str(TheSettings.getSettingOrSetDefault('ThreadCount', 4)))
      elif dbScenario == 'Connect to Remote Database':
         # Do not prefill credentials from settings per security/UX policy
         self.remoteHostEdit.setText('')
         self.remotePortEdit.setText('')
         self.remoteUserEdit.setText('')

   def validateSettings(self, interactive=True):
      """Validate directory paths group and create as needed (interactive)."""
      paths = self._collectPathsGroup()
      corePath = paths['core']
      armoryPath = paths['armory']
      dbPath = paths['db']

      # For non-interactive validation (unit tests), just check if paths exist
      if not interactive:
         return (os.path.exists(corePath) and os.path.exists(armoryPath) and
            os.path.exists(dbPath))

      # Interactive validation with directory creation dialogs
      if not os.path.exists(corePath):
         reply = QtWidgets.QMessageBox.question(
            self,
            self.tr('Create Directory?'),
            self.tr('Bitcoin Core data directory does not exist. '
               'Would you like to create it?'),
            (QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No |
             QtWidgets.QMessageBox.Cancel)
         )

         if reply == QtWidgets.QMessageBox.Yes:
            try:
               os.makedirs(corePath)
            except Exception as e:
               QtWidgets.QMessageBox.critical(
                  self,
                  self.tr('Error'),
                  self.tr('Could not create Bitcoin Core data directory: '
                     '{}').format(str(e))
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
               # Recursive check with new directory
               return self.validateSettings()
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
      scenarioOk = self.scenarioCombo.currentIndex() in (0, 1)
      modeOk = self.networkModeCombo.currentText() in (
         'Mainnet', 'Testnet', 'Regtest')
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

   def _validateWalletsGroup(self):
      # Do not probe bridge readiness here; UI handles it during listing
      return True

   def _validateDbGroup(self):
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
      isLocal = dbScenario == 'Run Local Database'
      isRemote = dbScenario == 'Connect to Remote Database'
      
      return {
         'scenario': dbScenario,
         'typeDisp': self.databaseTypeCombo.currentText() if isLocal else '',
         'remoteHost': self.remoteHostEdit.text() if isRemote else '',
         'remotePort': self.remotePortEdit.text() if isRemote else '',
         'remoteUser': self.remoteUserEdit.text() if isRemote else '',
         # remotePass removed - bridge handles password prompts
         'ram': self.ramUsageEdit.text() if isLocal else '',
         'threads': self.threadCountEdit.text() if isLocal else '',
      }

   def _applySettingsGroups(self, paths, core, db):
      """Persist settings and propagate runtime options from grouped dicts."""
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
      if self.main:
         # Main window implements setSatoshiPaths; call best-effort
         self.main.setSatoshiPaths()

   def saveSettings(self):
      """Save directory paths, database settings, and update bridge args."""
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
