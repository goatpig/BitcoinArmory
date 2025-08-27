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
   OS_MACOSX, OS_WINDOWS, CLI_OPTIONS, getBridgeArgList, LOGINFO, LOGEXCEPT
from armoryengine.Settings import TheSettings
from armoryengine.CppBridge import TheBridge
from armoryengine.BDM import TheBDM

from qtdialogs.ArmoryDialog import ArmoryDialog
from armorycolors import htmlColor
from qtdialogs.DlgMigrateWallet import DlgMigrateWallet
from qtdialogs.DlgUnlockWallet import UnlockWalletHandler
from qtdialogs.qtdefines import (
   UI_DIALOG_SPACING, UI_FRAME_MARGIN, UI_FRAME_PADDING, UI_BUTTON_SPACING, UI_GRID_SPACING,
   UI_STYLE_BUTTON_STANDARD, UI_STYLE_BUTTON_DIALOG, UI_STYLE_INPUT, UI_STYLE_COMBOBOX,
   UI_STYLE_FRAME, UI_STYLE_TREEWIDGET, apply_dialog_base_style,
   createStyledLabel, createButtonLayout, createInputField, createStyledButton, createStyledCombo,
   ComboBoxStyle
)

# --- Dialog-specific constants ---
MINIMUM_DIALOG_WIDTH = 850
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
      self.loadWalletList()

      # Display dialog unless in testing mode
      if not testing:
         self.show()

   def initMemberVariables(self, main):
      """Initialize all member variables with default values."""
      self.main = main
      self.comboStyle = ComboBoxStyle()
      self.bitcoinCoreProcess = None
      
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

   def setupDialogProperties(self):
      """Configure basic dialog properties and styling."""
      self.setMinimumWidth(MINIMUM_DIALOG_WIDTH)
      self.setMinimumHeight(MINIMUM_DIALOG_HEIGHT)
      self.setWindowTitle(self.tr('Armory Setup Manager'))
      self.setWindowFlags(QtCore.Qt.Window | QtCore.Qt.WindowStaysOnTopHint)
      self.setModal(True)
      # Apply base dialog styling
      apply_dialog_base_style(self)

   def initTabs(self):
      """Initialize all tab widgets."""
      try:
         # Create and style the tab container
         self.tabWidget = QtWidgets.QTabWidget()
         self.tabWidget.setContentsMargins(UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING)

         # Initialize individual tab widgets
         LOGINFO("Creating wallet tab...")
         # Ensure the bridge is running so wallet listing works in this dialog
         try:
            if (TheBDM is not None and
               not getattr(TheBridge.bridgeSocket, 'run', False)):
               TheBDM.startBridge(getBridgeArgList(), lambda *_: None)
         except Exception:
            pass
         self.walletTab = self.createWalletTab()
         
         LOGINFO("Creating core tab...")
         self.coreTab = self.createCoreTab()
         
         LOGINFO("Creating database tab...")
         self.databaseTab = self.createDatabaseTab()

         # Configure tab order
         self.tabWidget.addTab(self.walletTab, self.tr('Wallet Settings'))
         self.tabWidget.addTab(self.coreTab, self.tr('Core Settings'))
         self.tabWidget.addTab(self.databaseTab, self.tr('Database Settings'))
         
         LOGINFO("Tabs created successfully")
         
      except Exception as e:
         LOGEXCEPT("Error creating tabs: %s", str(e))
         raise

   def setupMainLayout(self):
      """Set up the main layout with tabs and buttons."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setSpacing(UI_DIALOG_SPACING)
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
      bottomLayout.setContentsMargins(UI_FRAME_MARGIN, UI_FRAME_PADDING, UI_FRAME_MARGIN, UI_FRAME_PADDING)
      bottomLayout.setSpacing(UI_BUTTON_SPACING)

      # Configure Accept/Cancel buttons
      buttonBox = QtWidgets.QDialogButtonBox()
      self.acceptButton = buttonBox.addButton(self.tr('Accept'),
         QtWidgets.QDialogButtonBox.AcceptRole)
      self.cancelButton = buttonBox.addButton(self.tr('Cancel'), 
         QtWidgets.QDialogButtonBox.RejectRole)

      # Style the buttons
      self.acceptButton.setStyleSheet(UI_STYLE_BUTTON_DIALOG)
      self.cancelButton.setStyleSheet(UI_STYLE_BUTTON_DIALOG)
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
         self.networkModeCombo.currentIndexChanged.connect(self.networkModeChanged)

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
         # Validate user inputs and ensure directories exist
         if not self.validateSettings(interactive=True):
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
      """Handle dialog close event to gracefully shut down Bitcoin Core if needed."""
      try:
         if self.bitcoinCoreProcess is not None:
            # Send SIGTERM signal to gracefully shut down
            self.bitcoinCoreProcess.terminate()
            # Wait for process to terminate
            self.bitcoinCoreProcess.wait(timeout=30)
      except Exception as e:
         LOGEXCEPT("Error shutting down Bitcoin Core: %s", str(e))
      finally:
         super().closeEvent(event)

   def createCoreTab(self):
      """Create the Core settings tab with directory and network configuration."""
      try:
         tab = QtWidgets.QWidget()
         mainLayout = QtWidgets.QVBoxLayout()
         mainLayout.setContentsMargins(UI_FRAME_MARGIN, UI_FRAME_MARGIN, UI_FRAME_MARGIN, UI_FRAME_MARGIN)
         mainLayout.setSpacing(UI_FRAME_PADDING)

         # Directory Settings Frame
         LOGINFO("Creating directory frame...")
         dirFrame = self.createDirectoryFrame()
         mainLayout.addWidget(dirFrame)

         # Core Settings Frame  
         LOGINFO("Creating core settings frame...")
         coreFrame = self.createCoreSettingsFrame()
         mainLayout.addWidget(coreFrame)
         
         mainLayout.addStretch()
         tab.setLayout(mainLayout)
         
         LOGINFO("Core tab created successfully")
         return tab

      except Exception as e:
         LOGEXCEPT("Error creating core tab: %s", str(e))
         raise

   def createDirectoryFrame(self):
      """Create the directory settings frame for the Core tab."""
      dirFrame = QtWidgets.QFrame()
      dirFrame.setStyleSheet(UI_STYLE_FRAME)
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING)
      dirFrameLayout.setSpacing(UI_GRID_SPACING)

      # Create grid layout for directory settings
      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(UI_GRID_SPACING)

      # Bitcoin Core Data Directory
      coreDirLabel = createStyledLabel(self.tr("Bitcoin Core Data Directory"))

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(UI_GRID_SPACING)
      self.satoshiHomePath = createInputField(width=400, style=UI_STYLE_INPUT)
      self.satoshiBrowseButton = createStyledButton(
         self.tr("Browse..."), width=100, style=UI_STYLE_BUTTON_STANDARD)

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
      coreFrame = QtWidgets.QFrame()
      coreFrame.setStyleSheet(UI_STYLE_FRAME)
      coreFrameLayout = QtWidgets.QVBoxLayout(coreFrame)
      coreFrameLayout.setContentsMargins(UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING)
      coreFrameLayout.setSpacing(UI_GRID_SPACING)

      # Create grid layout for core settings
      grid = QtWidgets.QGridLayout()
      grid.setSpacing(UI_GRID_SPACING)

      # Operation Mode
      operationLabel = createStyledLabel(self.tr("Operation Mode"))
      self.scenarioCombo = createStyledCombo(width=200, style=UI_STYLE_COMBOBOX)
      self.scenarioCombo.addItem(self.tr("Let Armory Automate It"))
      self.scenarioCombo.addItem(self.tr("Run Manually"))
      self.scenarioCombo.setStyle(self.comboStyle)
      self.scenarioCombo.view().setStyleSheet(
         f"background-color: {htmlColor('SlightBkgdDark')}; color: {htmlColor('Foreground')};")
      self.scenarioCombo.setEditable(False)
      self.scenarioCombo.setFrame(False)

      # Network Mode
      networkLabel = createStyledLabel(self.tr("Network Mode"))
      self.networkModeCombo = createStyledCombo(width=200, style=UI_STYLE_COMBOBOX)
      self.networkModeCombo.addItem(self.tr("Mainnet"))
      self.networkModeCombo.addItem(self.tr("Testnet"))
      self.networkModeCombo.addItem(self.tr("Regtest"))
      self.networkModeCombo.setStyle(self.comboStyle)

      # Bitcoin P2P Port
      p2pPortLabel = createStyledLabel(self.tr("Bitcoin P2P Port"))
      self.p2pPortInput = createInputField(width=100, style=UI_STYLE_INPUT)
      self.p2pPortInput.setText("8333")

      # RPC Port (grayed out placeholder)
      rpcPortLabel = createStyledLabel(self.tr("RPC Port"), color="DisableFG")
      self.rpcPortInput = createInputField(width=100, style=UI_STYLE_INPUT)
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

      # Auto-detect button
      autoDetectButton = createStyledButton(
         self.tr("Auto-detect Core Settings"), width=200, style=UI_STYLE_BUTTON_STANDARD)
      autoDetectButton.clicked.connect(self.autoDetectCoreSettings)

      # Add button with center alignment
      buttonLayout = createButtonLayout(autoDetectButton)
      coreFrameLayout.addLayout(buttonLayout)

      return coreFrame

   def scenarioChanged(self, index):
      """Handle changes to the scenario selection."""
      if index == 0:  # Let Armory Automate It
         self.p2pPortInput.setEnabled(False)
         self.rpcPortInput.setEnabled(False)  # Keep RPC port disabled
      elif index == 1:  # Run Manually
         self.p2pPortInput.setEnabled(True)
         self.rpcPortInput.setEnabled(False)  # Keep RPC port disabled

   def autoDetectCoreSettings(self):
      """Attempt to auto-detect existing Bitcoin Core settings and manage the node."""
      try:
         # Try to find Bitcoin Core data directory
         if OS_WINDOWS:
            defaultDir = os.path.expanduser('~\\AppData\\Roaming\\Bitcoin')
         elif OS_MACOSX:
            defaultDir = os.path.expanduser('~/Library/Application Support/Bitcoin')
         else:
            defaultDir = os.path.expanduser('~/.bitcoin')

         if os.path.exists(defaultDir):
            self.satoshiHomePath.setText(defaultDir)

            # Check if Bitcoin Core is already running
            isRunning = self.checkBitcoinCoreRunning()

            if isRunning:
               # Focus on existing instance
               self.focusBitcoinCoreInstance()
            else:
               # Start Bitcoin Core node
               self.startBitcoinCoreNode()

            # Try to read bitcoin.conf for RPC settings
            confPath = os.path.join(defaultDir, 'bitcoin.conf')
            if os.path.exists(confPath):
               with open(confPath, 'r') as f:
                  for line in f:
                     if line.startswith('rpcport='):
                        self.rpcPortInput.setText(line.strip().split('=')[1])
                     elif line.startswith('testnet='):
                        if line.strip().split('=')[1] == '1':
                           self.networkModeCombo.setCurrentText('Testnet')
                     elif line.startswith('regtest='):
                        if line.strip().split('=')[1] == '1':
                           self.networkModeCombo.setCurrentText('Regtest')

            QtWidgets.QMessageBox.information(
               self,
               self.tr('Settings Detected'),
               self.tr('Bitcoin Core settings have been auto-detected. Please verify the values.')
            )
         else:
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Settings Not Found'),
               self.tr('Could not find Bitcoin Core settings. Please enter them manually.')
            )
      except Exception as e:
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Error'),
            self.tr('Failed to auto-detect settings: {}').format(str(e))
         )

   def checkBitcoinCoreRunning(self):
      """Check if Bitcoin Core is already running."""
      try:
         # Try to connect to the RPC interface
         if OS_WINDOWS:
            import psutil
            for proc in psutil.process_iter(['name']):
               if proc.info['name'] == 'bitcoind.exe':
                  return True
         else:
            import psutil
            for proc in psutil.process_iter(['name']):
               if proc.info['name'] == 'bitcoind':
                  return True
         return False
      except Exception:
         return False

   def focusBitcoinCoreInstance(self):
      """Focus on the existing Bitcoin Core instance."""
      try:
         if OS_WINDOWS:
            import win32gui
            import win32con
            hwnd = win32gui.FindWindow(None, "Bitcoin Core")
            if hwnd:
               win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
               win32gui.SetForegroundWindow(hwnd)
      except Exception as e:
         LOGEXCEPT("Error focusing Bitcoin Core window: %s", str(e))

   def startBitcoinCoreNode(self):
      """Start the Bitcoin Core node."""
      try:
         # Get the Bitcoin Core executable path
         if OS_WINDOWS:
            bitcoindPath = os.path.join(os.path.dirname(self.satoshiHomePath.text()), 'bitcoind.exe')
         else:
            bitcoindPath = 'bitcoind'

         if not os.path.exists(bitcoindPath):
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Bitcoin Core Not Found'),
               self.tr('Could not find Bitcoin Core executable. Please install Bitcoin Core first.')
            )
            return False

         # Start Bitcoin Core with appropriate arguments
         import subprocess
         args = [bitcoindPath, '-datadir=' + self.satoshiHomePath.text()]

         # Add network mode arguments
         if self.networkModeCombo.currentText() == 'Testnet':
            args.append('-testnet')
         elif self.networkModeCombo.currentText() == 'Regtest':
            args.append('-regtest')

         # Start the process
         self.bitcoinCoreProcess = subprocess.Popen(args)
         return True
      except Exception as e:
         LOGEXCEPT("Error starting Bitcoin Core: %s", str(e))
         return False

   def showEvent(self, event):
      """Handle show event to ensure the dialog is properly displayed."""
      super().showEvent(event)
      # Make sure the dialog is visible and active
      self.setWindowState(self.windowState() & ~QtCore.Qt.WindowMinimized | QtCore.Qt.WindowActive)
      self.activateWindow()
      self.raise_()

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
      mainLayout.setContentsMargins(UI_FRAME_MARGIN, UI_FRAME_MARGIN, UI_FRAME_MARGIN, UI_FRAME_MARGIN)
      mainLayout.setSpacing(UI_FRAME_PADDING)

      # Directory Settings Frame
      dirFrame = QtWidgets.QFrame()
      dirFrame.setStyleSheet(UI_STYLE_FRAME)
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING)
      dirFrameLayout.setSpacing(UI_GRID_SPACING)

      # Create grid layout for directory settings
      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(UI_GRID_SPACING)

      # Armory Data Directory
      dirLabel = QtWidgets.QLabel(self.tr("Armory Data Directory"))
      dirLabel.setStyleSheet("color: white;")
      dirLabel.setToolTip(
         'The directory where Armory will store all wallet files and settings.\n\n'
         'This directory should be:\n'
         '• On a secure drive\n'
         '• Regularly backed up\n'
         '• Have sufficient free space'
      )

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(UI_GRID_SPACING)
      self.armoryDataDirEdit = QtWidgets.QLineEdit()
      self.armoryDataDirEdit.setMinimumWidth(400)
      self.armoryDataDirEdit.setStyleSheet(UI_STYLE_INPUT)

      browseBtn = QtWidgets.QPushButton(self.tr("Browse..."))
      browseBtn.setFixedWidth(100)  # Match Accept/Cancel buttons
      browseBtn.clicked.connect(lambda: self.browseDirDialog(self.armoryDataDirEdit))
      browseBtn.setStyleSheet(UI_STYLE_BUTTON_STANDARD)
      browseBtn.setToolTip(
         'Click to browse for a directory to store Armory data.\n\n'
         'Choose a location that is:\n'
         '• Secure and private\n'
         '• Has sufficient space\n'
         '• Is on a reliable drive'
      )

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
      walletFrame = QtWidgets.QFrame()
      walletFrame.setStyleSheet(UI_STYLE_FRAME)
      walletFrameLayout = QtWidgets.QVBoxLayout(walletFrame)
      walletFrameLayout.setContentsMargins(UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING)
      walletFrameLayout.setSpacing(UI_GRID_SPACING)

      # Create grid layout for wallet list settings
      walletGrid = QtWidgets.QGridLayout()
      walletGrid.setSpacing(UI_GRID_SPACING)

      # Wallet List Title
      walletTitle = QtWidgets.QLabel(self.tr("Available Wallets"))
      walletTitle.setStyleSheet("""
         QLabel {
            color: white;
            font-size: 14px;
            font-weight: bold;
         }
      """)
      walletTitle.setToolTip(
         'Displays all Bitcoin wallets found in the selected directory.\n\n'
         'The list shows:\n'
         '• Wallet IDs - Unique identifiers for each wallet\n'
         '• Names - Custom labels for easy identification\n'
         '• Types - Standard, Offline, or Backup\n'
         '• Actions - Load or Decrypt options'
      )

      # Wallet List
      self.walletList = QtWidgets.QTreeWidget()
      self.walletList.setHeaderLabels(['Wallet ID', 'Name', 'Type', 'Actions'])
      self.walletList.setColumnWidth(0, 200)
      self.walletList.setColumnWidth(1, 150)
      self.walletList.setColumnWidth(2, 100)
      self.walletList.setSelectionMode(QtWidgets.QAbstractItemView.NoSelection)
      self.walletList.setIndentation(0)
      self.walletList.setRootIsDecorated(False)
      self.walletList.setStyleSheet(UI_STYLE_TREEWIDGET)

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
      self.refreshButton.setStyleSheet(UI_STYLE_BUTTON_STANDARD)
      self.refreshButton.setToolTip(
         'Click to refresh the list of available wallets.\n\n'
         'Use this when you:\n'
         '• Add new wallets to the directory\n'
         '• Remove wallets from the directory\n'
         '• Want to ensure the list is up to date'
      )

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
      try:
         # Ask bridge; if bridge not ready, show a placeholder and exit
         if (TheBDM is None or
            not getattr(TheBridge.bridgeSocket, 'run', False) or
            not TheBridge.bridgeSocket.bip15xConnection.ready()):
            placeholder = QtWidgets.QTreeWidgetItem()
            placeholder.setText(0, self.tr('Bridge is not running yet'))
            placeholder.setText(1, '')
            placeholder.setText(2, '')
            placeholder.setText(3, '')
            self.walletList.addTopLevelItem(placeholder)
            return

         # List wallets using wallet manager (returns a List of WalletFileData)
         wltList = TheBridge.wltManager.listWallets()
         # Iterate by index to satisfy capnp dynamic list reader
         for idx in range(len(wltList)):
            entry = wltList[idx]
            # Derive display values
            walletPath = getattr(entry, 'path', '')
            fileName = os.path.basename(walletPath) if walletPath else ''
            walletId = getattr(entry, 'walletId', '')
            loadState = getattr(entry, 'state', 0)

            # Create row
            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, walletId if walletId else fileName)
            item.setText(1, getattr(entry, 'title', walletId or fileName))

            # Determine state and action
            # WalletManagerReply.WalletLoadState: 1=legacy, 3=encrypted, 4=ready
            # loadState is a capnp enum; rely on underlying numeric value via string compare
            loadStateStr = str(loadState)
            isLegacy = (loadStateStr.endswith('.legacy')) or (not bool(walletId))
            if isLegacy:
               stateText = 'Legacy'
            elif loadStateStr.endswith('.encrypted'):
               stateText = 'Encrypted (locked)'
            else:
               stateText = 'Ready'
            item.setText(2, stateText)

            # Action widget
            actionWidget = QtWidgets.QWidget()
            actionLayout = QtWidgets.QHBoxLayout(actionWidget)
            actionLayout.setContentsMargins(0, 0, 0, 0)
            actionBtn = QtWidgets.QPushButton()
            actionBtn.setStyleSheet(UI_STYLE_BUTTON_STANDARD)

            fullPath = walletPath if walletPath else os.path.join(self.armoryDataDirEdit.text(), fileName)

            if isLegacy:
               actionBtn.setText(self.tr('Migrate'))
               actionBtn.clicked.connect(lambda _, p=fullPath: self._migrateWallet(p))
            elif loadStateStr.endswith('.encrypted'):
               actionBtn.setText(self.tr('Decrypt'))
               actionBtn.clicked.connect(lambda _, wid=walletId: self._unlockWallet(wid))
            else:
               actionBtn.setText(self.tr('Load'))
               actionBtn.clicked.connect(lambda _, wid=walletId: self._loadWallet(wid))

            actionLayout.addWidget(actionBtn)
            self.walletList.addTopLevelItem(item)
            self.walletList.setItemWidget(item, 3, actionWidget)

      except Exception as e:
         LOGEXCEPT('Error loading wallet list: %s', str(e))

   def _migrateWallet(self, walletPath):
      try:
         # DlgMigrateWallet(promptId, wltID, verbose, parent=None, main=None)
         DlgMigrateWallet(None, walletPath, self.tr('Migrate'), self, self).exec_()
         TheBridge.wltManager.loadWallets()
         self.loadWalletList()
      except Exception as e:
         QtWidgets.QMessageBox.warning(self, self.tr('Migration Failed'), str(e))

   def _unlockWallet(self, walletId):
      try:
         # Control header unlock requires a path; list again to find the entry
         wltList = TheBridge.wltManager.listWallets()
         pathForId = None
         for key in wltList:
            entry = wltList[key]
            if getattr(entry, 'walletId', '') == walletId:
               pathForId = key
               break
         if not pathForId:
            QtWidgets.QMessageBox.warning(self, self.tr('Unlock Failed'), self.tr('Wallet not found.'))
            return
         # Use UnlockWalletHandler which integrates with bridge prompts
         dlg = UnlockWalletHandler(walletId, self.tr('Unlock Wallet'), self)
         dlg.exec_()
         TheBridge.wltManager.loadWallets()
         self.loadWalletList()
      except Exception as e:
         QtWidgets.QMessageBox.warning(self, self.tr('Unlock Failed'), str(e))

   def _loadWallet(self, walletId):
      try:
         # Stage and refresh
         TheBridge.wltManager.stageWallet(walletId, True)
         TheBridge.wltManager.loadWallets()
         self.loadWalletList()
      except Exception as e:
         QtWidgets.QMessageBox.warning(self, self.tr('Load Failed'), str(e))

   def createDatabaseTab(self):
      tab = QtWidgets.QWidget()
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(UI_FRAME_MARGIN, UI_FRAME_MARGIN, UI_FRAME_MARGIN, UI_FRAME_MARGIN)
      mainLayout.setSpacing(UI_FRAME_PADDING)

      # Directory Settings Frame
      dirFrame = QtWidgets.QFrame()
      dirFrame.setStyleSheet(UI_STYLE_FRAME)
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING, UI_FRAME_PADDING)
      dirFrameLayout.setSpacing(UI_GRID_SPACING)

      # Database Directory
      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      dbDirLabel = QtWidgets.QLabel(self.tr("Database Directory"))
      dbDirLabel.setStyleSheet("color: white;")

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      self.databaseDirEdit = QtWidgets.QLineEdit()
      self.databaseDirEdit.setMinimumWidth(400)
      self.databaseDirEdit.setStyleSheet(UI_STYLE_INPUT)

      dbDirButton = QtWidgets.QPushButton(self.tr("Browse..."))
      dbDirButton.setFixedWidth(100)
      dbDirButton.clicked.connect(lambda: self.browseDirDialog(self.databaseDirEdit))
      dbDirButton.setStyleSheet(UI_STYLE_BUTTON_STANDARD)

      dirInputLayout.addWidget(self.databaseDirEdit)
      dirInputLayout.addWidget(dbDirButton)

      dirGrid.addWidget(dbDirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      dirFrameLayout.addLayout(dirGrid)
      mainLayout.addWidget(dirFrame)

      # Scenario Selection Frame
      scenarioFrame = QtWidgets.QFrame()
      scenarioFrame.setStyleSheet(UI_STYLE_FRAME)
      scenarioLayout = QtWidgets.QVBoxLayout(scenarioFrame)
      scenarioLayout.setContentsMargins(16, 16, 16, 16)
      scenarioLayout.setSpacing(8)

      scenarioGrid = QtWidgets.QGridLayout()
      scenarioGrid.setSpacing(8)

      # Database Scenario
      dbScenarioLabel = QtWidgets.QLabel(self.tr("Database Scenario"))
      dbScenarioLabel.setStyleSheet("color: white;")
      self.databaseScenarioCombo = QtWidgets.QComboBox()
      self.databaseScenarioCombo.setFixedWidth(200)
      self.databaseScenarioCombo.addItems(["Run Local Database", "Connect to Remote Database", "No Database"])
      self.databaseScenarioCombo.setStyleSheet(UI_STYLE_COMBOBOX)
      self.databaseScenarioCombo.currentIndexChanged.connect(self.handleDatabaseScenarioChange)

      scenarioGrid.addWidget(dbScenarioLabel, 0, 0)
      scenarioGrid.addWidget(self.databaseScenarioCombo, 0, 1)
      scenarioGrid.setColumnStretch(1, 1)

      scenarioLayout.addLayout(scenarioGrid)
      mainLayout.addWidget(scenarioFrame)

      # Local Database Settings Frame
      self.localDatabaseFrame = QtWidgets.QFrame()
      self.localDatabaseFrame.setStyleSheet(UI_STYLE_FRAME)
      localDbLayout = QtWidgets.QVBoxLayout(self.localDatabaseFrame)
      localDbLayout.setContentsMargins(16, 16, 16, 16)
      localDbLayout.setSpacing(8)

      localDbGrid = QtWidgets.QGridLayout()
      localDbGrid.setSpacing(8)

      # Database Type
      dbTypeLabel = QtWidgets.QLabel(self.tr("Database Type"))
      dbTypeLabel.setStyleSheet("color: white;")
      self.databaseTypeCombo = QtWidgets.QComboBox()
      self.databaseTypeCombo.setFixedWidth(200)
      self.databaseTypeCombo.addItems(["Full Database", "Supernode"])
      self.databaseTypeCombo.setStyleSheet(UI_STYLE_COMBOBOX)

      # RAM Usage
      ramLabel = QtWidgets.QLabel(self.tr("RAM Usage (MB)"))
      ramLabel.setStyleSheet("color: white;")
      self.ramUsageEdit = QtWidgets.QLineEdit()
      self.ramUsageEdit.setFixedWidth(100)
      self.ramUsageEdit.setStyleSheet(UI_STYLE_INPUT)

      # Thread Count
      threadLabel = QtWidgets.QLabel(self.tr("Thread Count"))
      threadLabel.setStyleSheet("color: white;")
      self.threadCountEdit = QtWidgets.QLineEdit()
      self.threadCountEdit.setFixedWidth(100)
      self.threadCountEdit.setStyleSheet(UI_STYLE_INPUT)

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
      self.remoteFrame = QtWidgets.QFrame()
      self.remoteFrame.setStyleSheet(UI_STYLE_FRAME)
      remoteLayout = QtWidgets.QVBoxLayout(self.remoteFrame)
      remoteLayout.setContentsMargins(16, 16, 16, 16)
      remoteLayout.setSpacing(8)

      remoteGrid = QtWidgets.QGridLayout()
      remoteGrid.setSpacing(8)

      # Remote Host
      hostLabel = QtWidgets.QLabel(self.tr("Remote Host"))
      hostLabel.setStyleSheet("color: white;")
      self.remoteHostEdit = QtWidgets.QLineEdit()
      self.remoteHostEdit.setFixedWidth(200)
      self.remoteHostEdit.setStyleSheet(UI_STYLE_INPUT)

      # Remote Port
      portLabel = QtWidgets.QLabel(self.tr("Remote Port"))
      portLabel.setStyleSheet("color: white;")
      self.remotePortEdit = QtWidgets.QLineEdit()
      self.remotePortEdit.setFixedWidth(100)
      self.remotePortEdit.setStyleSheet(UI_STYLE_INPUT)

      # Remote Username
      userLabel = QtWidgets.QLabel(self.tr("Username"))
      userLabel.setStyleSheet("color: white;")
      self.remoteUserEdit = QtWidgets.QLineEdit()
      self.remoteUserEdit.setFixedWidth(200)
      self.remoteUserEdit.setStyleSheet(UI_STYLE_INPUT)

      # Remote Password
      passLabel = QtWidgets.QLabel(self.tr("Password"))
      passLabel.setStyleSheet("color: white;")
      self.remotePasswordEdit = QtWidgets.QLineEdit()
      self.remotePasswordEdit.setFixedWidth(200)
      self.remotePasswordEdit.setEchoMode(QtWidgets.QLineEdit.Password)
      self.remotePasswordEdit.setStyleSheet(UI_STYLE_INPUT)

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
      self.testConnectionButton.setStyleSheet(UI_STYLE_BUTTON_STANDARD)
      
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
      """Test the connection to the remote database."""
      try:
         # Get connection details
         host = self.remoteHostEdit.text()
         port = self.remotePortEdit.text()
         user = self.remoteUserEdit.text()
         password = self.remotePasswordEdit.text()

         if not all([host, port, user, password]):
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Missing Information'),
               self.tr('Please fill in all remote database connection details.')
            )
            return

         # Attempt to connect to the remote database
         import socket
         try:
            # Create a socket and attempt to connect
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)  # 5 second timeout
            result = sock.connect_ex((host, int(port)))
            sock.close()

            if result == 0:
               QtWidgets.QMessageBox.information(
                  self,
                  self.tr('Connection Test'),
                  self.tr('Successfully connected to remote database.')
               )
            else:
               QtWidgets.QMessageBox.warning(
                  self,
                  self.tr('Connection Test'),
                  self.tr('Could not connect to {}:{}'.format(host, port))
               )
         except ValueError:
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Connection Test'),
               self.tr('Invalid port number. Please enter a valid integer.')
            )
      except Exception as e:
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Connection Error'),
            self.tr('Failed to connect to remote database: {}').format(str(e))
         )

   def loadSettings(self, testing=False):
      """Load settings from configuration including directory paths and database settings.

      Args:
          testing: Whether this is being used for testing (uses empty defaults if True)
      """
      # Load directory paths with appropriate defaults based on testing mode
      # Always display current effective paths, defaulting to engine defaults
      self.satoshiHomePath.setText(os.path.normpath(
         TheSettings.getSettingOrSetDefault('CoreDataDir', BTC_HOME_DIR)))
      self.armoryDataDirEdit.setText(os.path.normpath(
         TheSettings.getSettingOrSetDefault('ArmoryDataDir', ARMORY_HOME_DIR)))
      self.databaseDirEdit.setText(os.path.normpath(
         TheSettings.getSettingOrSetDefault('DBDir', ARMORY_DB_DIR)))

      # Determine if we have existing Core settings
      hasCoreSettings = bool(self.satoshiHomePath.text() and os.path.exists(self.satoshiHomePath.text()))
      self.scenarioCombo.setCurrentIndex(0 if hasCoreSettings else 1)

      # Load Core settings
      networkMode = TheSettings.getSettingOrSetDefault('NetworkMode', 'Mainnet')
      self.networkModeCombo.setCurrentText(networkMode)
      
      # Load database configuration settings
      dbScenario = TheSettings.getSettingOrSetDefault('DBScenario', 'Run Local Database')
      self.databaseScenarioCombo.setCurrentText(dbScenario)

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
         self.remoteHostEdit.setText(TheSettings.getSettingOrSetDefault('RemoteDBHost', ''))
         self.remotePortEdit.setText(TheSettings.getSettingOrSetDefault('RemoteDBPort', ''))
         self.remoteUserEdit.setText(TheSettings.getSettingOrSetDefault('RemoteDBUser', ''))
         self.remotePasswordEdit.setText(TheSettings.getSettingOrSetDefault('RemoteDBPass', ''))

   def validateSettings(self, interactive=True):
      """Validate directory paths and create them if they don't exist. Shows appropriate dialogs for user interaction."""
      # Get current directory paths from input fields
      corePath = self.satoshiHomePath.text()
      armoryPath = self.armoryDataDirEdit.text()
      dbPath = self.databaseDirEdit.text()

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

   def saveSettings(self):
      """Save directory paths, database settings, and update bridge arguments in the configuration."""
      try:
         # Save directory paths to configuration
         TheSettings.set('CoreDataDir', self.satoshiHomePath.text())
         TheSettings.set('ArmoryDataDir', self.armoryDataDirEdit.text())
         TheSettings.set('DBDir', self.databaseDirEdit.text())

         # Ensure backend-consumed keys are set for Bitcoin Core path management
         TheSettings.set('SatoshiDatadir', self.satoshiHomePath.text())
         # ManageSatoshi: enable when automation selected
         TheSettings.set('ManageSatoshi', self.scenarioCombo.currentIndex() == 0)

         # Save Core settings
         TheSettings.set('NetworkMode', self.networkModeCombo.currentText())

         # Save database configuration settings
         dbScenario = self.databaseScenarioCombo.currentText()
         TheSettings.set('DBScenario', dbScenario)

         if dbScenario == 'Run Local Database':
            # Map display to backend value
            dbTypeDisp = self.databaseTypeCombo.currentText()
            dbTypeVal = 'DB_SUPER' if dbTypeDisp == 'Supernode' else 'DB_FULL'
            TheSettings.set('DBType', dbTypeVal)
            # Propagate to CLI options used by bridge args
            CLI_OPTIONS.db_type = dbTypeVal
            TheSettings.set('RAMUsage', int(self.ramUsageEdit.text()))
            TheSettings.set('ThreadCount', int(self.threadCountEdit.text()))
         elif dbScenario == 'Connect to Remote Database':
            TheSettings.set('RemoteDBHost', self.remoteHostEdit.text())
            TheSettings.set('RemoteDBPort', self.remotePortEdit.text())
            TheSettings.set('RemoteDBUser', self.remoteUserEdit.text())
            TheSettings.set('RemoteDBPass', self.remotePasswordEdit.text())
            # Propagate to CLI options used by bridge args
            CLI_OPTIONS.armorydb_ip = self.remoteHostEdit.text()
            CLI_OPTIONS.armorydb_port = self.remotePortEdit.text()

         # Propagate paths to CLI options for bridge
         CLI_OPTIONS.datadir = self.armoryDataDirEdit.text()
         CLI_OPTIONS.armoryDBDir = self.databaseDirEdit.text()

         # Update bridge paths in main window (non-fatal if not available)
         try:
            if self.main and hasattr(self.main, 'setSatoshiPaths'):
               self.main.setSatoshiPaths()
         except Exception:
            LOGEXCEPT('Failed to notify main window to refresh Satoshi paths')
      except Exception as e:
         LOGEXCEPT("Error saving settings: %s", str(e))

   @staticmethod
   def run(parent=None, main=None, testing=False):
      """Optional helper for callers that want to show the dialog.

      Note: This function is intentionally simple and non-invasive. It does
      not start the bridge, show splash screens, or manage the main window.
      """
      dlg = DlgSetupManager(parent=parent, main=main, testing=testing)
      return dlg.exec_()
