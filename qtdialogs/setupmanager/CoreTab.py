################################################################################
#                                                                              #
#  Copyright (C) 2026, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

import os

from qtpy import QtCore, QtWidgets
from armoryengine.CppBridge import TheBridge
from armoryengine.ArmoryUtils import USE_TESTNET, USE_REGTEST, BITCOIN_PORT, \
   BITCOIN_RPC_PORT
from armoryengine.Settings import TheSettings

import qtdialogs.qtdefines as qtdefines

# Core operation scenarios
SCENARIO_CORE_AUTOMATE = "Let Armory Automate It"
SCENARIO_CORE_MANUAL = "Run Manually"

################################################################################
class CoreTab(QtWidgets.QWidget):
   """
   Core settings tab for Setup Manager.

   Manages:
   - Bitcoin Core data directory selection
   - Operation mode (automate vs manual)
   - Network mode display (read-only, set by CLI)
   - Port configuration
   """
   def __init__(self, parent, main=None):
      super().__init__(parent)
      self.btcDir = None
      self.btcBin = None

      self.satoshiHomePath = None
      self.satoshiBrowseButton = None
      self.chainSize = None
      self.prunedState = None

      self.satoshiBinPath = None
      self.satoshiBinBrowse = None
      self.satoshiBinVer = None

      self.scenarioCombo = None
      self.networkModeCombo = None
      self.p2pPortInput = None
      self.rpcPortInput = None
      self.coreDirectoryFrame = None
      self.coreBinFrame = None
      self.coreSettingsFrame = None
      self.initUI()

   def initUI(self):
      """Initialize the Core settings tab UI."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      title = qtdefines.createTabTitle(self.tr('Core Settings'))
      mainLayout.addWidget(title)

      self.createDirectoryFrame()
      mainLayout.addWidget(self.coreDirectoryFrame)
      mainLayout.addWidget(self.coreBinFrame)

      self.createSettingsFrame()
      mainLayout.addWidget(self.coreSettingsFrame)

      mainLayout.addStretch()
      self.setLayout(mainLayout)

   def createDirectoryFrame(self):
      """Create the directory settings frame for the Core tab."""
      self.coreDirectoryFrame = QtWidgets.QGroupBox(self.tr('Satoshi Home'))
      dirFrameLayout = QtWidgets.QVBoxLayout(self.coreDirectoryFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      dirInputLayout = QtWidgets.QGridLayout()
      dirInputLayout.setSpacing(8)

      #satoshi datadir
      coreDirLabel = QtWidgets.QLabel(self.tr("Data Directory"))
      self.satoshiHomePath = QtWidgets.QLineEdit()
      self.satoshiHomePath.setReadOnly(True)
      self.satoshiBrowseButton = QtWidgets.QPushButton(self.tr("Browse..."))
      self.satoshiBrowseButton.setFixedWidth(100)
      self.satoshiBrowseButton.clicked.connect(self.browseSatoshiHome)
      self.chainSize = QtWidgets.QLabel()
      self.prunedState = QtWidgets.QLabel()

      dirInputLayout.addWidget(coreDirLabel              , 0, 0, 1, 1)
      dirInputLayout.addWidget(self.satoshiHomePath      , 0, 1, 1, 4)
      dirInputLayout.addWidget(self.satoshiBrowseButton  , 0, 5, 1, 1)
      dirInputLayout.addWidget(self.chainSize            , 1, 1, 1, 1)
      dirInputLayout.addWidget(self.prunedState          , 1, 3, 1, 1)
      dirFrameLayout.addLayout(dirInputLayout)

      #satoshi binary
      self.coreBinFrame = QtWidgets.QGroupBox(self.tr('Satoshi Executable'))
      binFrameLayout = QtWidgets.QVBoxLayout(self.coreBinFrame)
      binFrameLayout.setContentsMargins(12, 12, 12, 12)
      binFrameLayout.setSpacing(8)

      binLayout = QtWidgets.QGridLayout()
      binLayout.setSpacing(8)

      coreBinLabel = QtWidgets.QLabel(self.tr("Executable"))
      self.satoshiBinPath = QtWidgets.QLineEdit()
      self.satoshiBinPath.setReadOnly(True)
      self.satoshiBinBrowse = QtWidgets.QPushButton(self.tr("Browse..."))
      self.satoshiBinBrowse.setFixedWidth(100)
      self.satoshiBinBrowse.clicked.connect(self.browseSatoshiBin)
      self.satoshiBinVer = QtWidgets.QLabel()

      binLayout.addWidget(coreBinLabel                , 0, 0, 1, 1)
      binLayout.addWidget(self.satoshiBinPath         , 0, 1, 1, 4)
      binLayout.addWidget(self.satoshiBinBrowse       , 0, 5, 1, 1)
      binLayout.addWidget(self.satoshiBinVer          , 1, 1, 1, 1)
      binFrameLayout.addLayout(binLayout)

   def createSettingsFrame(self):
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
      self.scenarioCombo.currentIndexChanged.connect(self.onScenarioChanged)

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
      self.rpcPortInput.setToolTip(self.tr(
         "Standard Bitcoin Core RPC port "
         "(not configurable in Armory)"))

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
      if scenario == SCENARIO_CORE_MANUAL:
         self.coreBinFrame.setEnabled(False)
         self.p2pPortInput.setEnabled(True)
         self.rpcPortInput.setEnabled(True)
      elif scenario == SCENARIO_CORE_AUTOMATE:
         self.coreBinFrame.setEnabled(True)
         self.p2pPortInput.setEnabled(False)
         self.rpcPortInput.setEnabled(False)

   def browseSatoshiHome(self):
      """Open a directory dialog to select the Bitcoin Core data directory."""
      directory = QtWidgets.QFileDialog.getExistingDirectory(
         self,
         self.tr('Select Bitcoin Core Data Directory'),
         os.path.expanduser('~')
      )
      if directory:
         self._validateSatoshiDir(directory)

   def browseSatoshiBin(self):
      """Open a directory dialog to select the Bitcoin Core daemon path."""
      binpath = QtWidgets.QFileDialog.getOpenFileName(
         self,
         self.tr('Select Bitcoin Core Daemon'),
         os.path.expanduser('~')
      )
      if binpath:
         self._validateSatoshiBin(binpath[0])

   def loadSettings(self):
      """Load core tab settings from configuration."""
      # Load Bitcoin Core path from settings,
      # fallback to bridge auto detection routine
      savedPath = TheSettings.get('SatoshiDatadir')
      self.btcDir = savedPath if savedPath else None
      savedBin = TheSettings.get('SatoshiBin')
      self.btcBin = savedBin if savedBin else None

      # Load operation mode
      hasCoreSettings = bool(self.satoshiHomePath.text() and
         os.path.exists(self.satoshiHomePath.text()))
      manageSatoshi = TheSettings.get('ManageSatoshi')
      if manageSatoshi is not None:
         self.scenarioCombo.setCurrentText(
            SCENARIO_CORE_AUTOMATE if manageSatoshi else SCENARIO_CORE_MANUAL)
      else:
         self.scenarioCombo.setCurrentText(
            SCENARIO_CORE_AUTOMATE if hasCoreSettings else SCENARIO_CORE_MANUAL)

      # Set network mode from CLI flags (read-only)
      if USE_REGTEST:
         self.networkModeCombo.setCurrentText('Regtest')
      elif USE_TESTNET:
         self.networkModeCombo.setCurrentText('Testnet')
      else:
         self.networkModeCombo.setCurrentText('Mainnet')

      # Load port settings
      p2pPort = TheSettings.get('BitcoinP2PPort')
      rpcPort = TheSettings.get('BitcoinRPCPort')
      self.p2pPortInput.setText(str(p2pPort) if p2pPort else str(BITCOIN_PORT))
      self.rpcPortInput.setText(str(rpcPort) if rpcPort else str(BITCOIN_RPC_PORT))

   def collectSettings(self):
      """Return current core settings from UI as a dict."""
      scenario = self.scenarioCombo.currentText() == SCENARIO_CORE_AUTOMATE
      return {
         'datadir': self.btcDir,
         'binpath' : self.btcBin,
         'automate': True if scenario and self.btcDir and self.btcBin else False,
         'p2pPort': str(self.p2pPortInput.text()),
         'rpcPort': str(self.rpcPortInput.text()),
      }

   def validate(self):
      """Validate core tab settings. Returns True if valid."""
      if not self.btcDir:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Invalid Core Datadir'),
            self.tr('Please select a valid datadir for Bitcoin Core.')
         )
         return False

      automate = self.scenarioCombo.currentText() == SCENARIO_CORE_AUTOMATE
      if automate and not self.btcBin:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Invalid Core Executable'),
            self.tr(
               '''Armory needs the path to bitcoind to automate Core.
               \nPoint it to a valid executable or disable automation to proceed further.
               ''')
         )
         return False
      return True

   def _validateSatoshiDir(self, target):
      try:
         validationResult = \
            TheBridge.dbSetup.validateSatoshiDatadir(target)
         self.btcDir = os.path.normpath(validationResult.path)
         self.satoshiHomePath.setStyleSheet("color: black; font-style: normal;")
         self.satoshiHomePath.setText(self.btcDir)
         self.chainSize.setText(f"Chain Size: <b>{validationResult.chainSizeGB}GB</b>")

         prunedFlag = f"<b style=\"color: red;\">Pruned</b>" if validationResult.pruned else \
            f"<b style=\"color: green;\">No Pruning</b>"
         self.prunedState.setText(f"Chain Data: {prunedFlag}")
      except:
         self.btcDir = None
         self.satoshiHomePath.setStyleSheet("color: red; font-style: italic;")
         self.chainSize.setText("Chain Size: N/A")
         self.prunedState.setText("Chain Data: N/A")

   def _validateSatoshiBin(self, target):
      try:
         validationResult = \
            TheBridge.dbSetup.validateSatoshiBinary(target)
         self.btcBin = os.path.normpath(validationResult.path)
         self.satoshiBinPath.setStyleSheet("color: black; font-style: normal;")
         self.satoshiBinPath.setText(self.btcBin)
         self.satoshiBinVer.setText(
            f"Version: <b style=\"color: green;\">{validationResult.version}</b>")
      except Exception as e:
         self.btcBin = None
         self.satoshiBinPath.setStyleSheet("color: red; font-style: italic;")
         self.satoshiBinPath.setText(target)
         self.satoshiBinVer.setText(f"<i>{str(e)}</i>")

   def onBridgeReady(self):
      ## detect and/or validate satoshi datadir ##
      targetDir = self.btcDir if self.btcDir else \
         TheBridge.dbSetup.findSatoshiDatadir()
      self._validateSatoshiDir(targetDir)

      ## detect and/or validate satoshi binary ##
      targetBin = self.btcBin if self.btcBin else \
         TheBridge.dbSetup.findSatoshiBinary()
      self._validateSatoshiBin(targetBin)
