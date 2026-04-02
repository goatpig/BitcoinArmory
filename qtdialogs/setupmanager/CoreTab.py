################################################################################
#                                                                              #
#  Copyright (C) 2026, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

import os

from qtpy import QtCore, QtWidgets
from armoryengine.BDM import TheBDM
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
      self.main = main
      self.satoshiHomePath = None
      self.satoshiBrowseButton = None
      self.scenarioCombo = None
      self.networkModeCombo = None
      self.p2pPortInput = None
      self.rpcPortInput = None
      self.coreDirectoryFrame = None
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

      self.createSettingsFrame()
      mainLayout.addWidget(self.coreSettingsFrame)

      mainLayout.addStretch()
      self.setLayout(mainLayout)

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
      self.satoshiBrowseButton.clicked.connect(self.browseSatoshiHome)

      dirInputLayout = QtWidgets.QHBoxLayout()
      dirInputLayout.setSpacing(8)
      dirInputLayout.addWidget(self.satoshiHomePath)
      dirInputLayout.addWidget(self.satoshiBrowseButton)

      dirGrid.addWidget(coreDirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)

      dirFrameLayout.addLayout(dirGrid)

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
      if scenario in (SCENARIO_CORE_AUTOMATE, SCENARIO_CORE_MANUAL):
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
         self.satoshiHomePath.setText(directory)

   def loadSettings(self):
      """Load core tab settings from configuration."""
      # Load Bitcoin Core path from settings, fallback to TheBDM
      savedPath = TheSettings.get('SatoshiDatadir')
      if savedPath:
         btcDir = savedPath
      else:
         btcDir = TheBDM.btcdir
      self.satoshiHomePath.setText(os.path.normpath(btcDir))

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
      return {
         'corePath': str(self.satoshiHomePath.text()),
         'networkMode': str(self.networkModeCombo.currentText()),
         'manageSatoshi': (self.scenarioCombo.currentText() ==
            SCENARIO_CORE_AUTOMATE),
         'p2pPort': str(self.p2pPortInput.text()),
         'rpcPort': str(self.rpcPortInput.text()),
      }

   def validate(self):
      """Validate core tab settings. Returns True if valid."""
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

   def validateCorePath(self):
      """
      Validate Bitcoin Core data directory exists.
      Returns True if valid, False if user cancelled.
      Prompts user to select new directory if current doesn't exist.
      """
      corePath = str(self.satoshiHomePath.text())
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
               return self.validateCorePath()
         return False
      return True
