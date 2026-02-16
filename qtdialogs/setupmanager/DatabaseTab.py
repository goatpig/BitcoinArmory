################################################################################
#                                                                              #
#  Copyright (C) 2025, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

import os

from qtpy import QtCore, QtWidgets
from armoryengine.ArmoryUtils import ARMORY_DB_DIR
from armoryengine.Settings import TheSettings

import qtdialogs.qtdefines as qtdefines

# Database scenario constants
SCENARIO_DB_LOCAL = "Local Database"
SCENARIO_DB_REMOTE = "Remote Database"
SCENARIO_DB_NONE = "Offline"

# Validation limits
MAX_RAM_USAGE = 256      # Max RAM in 128MB increments (~32GB)
MAX_THREAD_COUNT = 64    # Max threads for DB operations

################################################################################
class DatabaseTab(QtWidgets.QWidget):
   """
   Database settings tab for Setup Manager.

   Manages:
   - Database directory selection
   - Database scenario (local/remote/offline)
   - Local database configuration (type, RAM, threads)
   - Remote database configuration (host, port, user)
   - CLI command display
   """
   def __init__(self, parent, main=None):
      super().__init__(parent)
      self.main = main
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
      self.cliCommandLabel = None
      self.dbBootstrapLabel = None
      self.initUI()

   def initUI(self):
      """Initialize the database settings tab UI."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      title = qtdefines.createTabTitle(self.tr('Database Settings'))
      mainLayout.addWidget(title)

      self.initWidgets()

      dirFrame = QtWidgets.QGroupBox(self.tr('Database Directory'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)

      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)

      dbDirLabel = QtWidgets.QLabel(self.tr("Location"))
      dirInputLayout, _ = qtdefines.createDirectoryInputLayout(
         self, self.databaseDirEdit, self.tr('Select Database Directory'))

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

      self.createLocalDatabaseFrame()
      mainLayout.addWidget(self.localDatabaseFrame)

      self.createRemoteFrame()
      mainLayout.addWidget(self.remoteFrame)

      cliFrame = QtWidgets.QGroupBox(self.tr('Generated Command Line'))
      cliLayout = QtWidgets.QVBoxLayout(cliFrame)
      cliLayout.setContentsMargins(12, 12, 12, 12)
      cliLayout.addWidget(self.cliCommandLabel)
      mainLayout.addWidget(cliFrame)

      mainLayout.addStretch()

      self.remoteFrame.hide()
      self.updateCliCommandDisplay()

      self.setLayout(mainLayout)

   def initWidgets(self):
      """Initialize database tab widgets as instance variables."""
      self.databaseDirEdit = QtWidgets.QLineEdit()
      self.databaseDirEdit.setMinimumWidth(400)
      self.databaseDirEdit.textChanged.connect(self.updateCliCommandDisplay)

      self.databaseScenarioCombo = QtWidgets.QComboBox()
      self.databaseScenarioCombo.setFixedWidth(200)
      self.databaseScenarioCombo.addItems([
         SCENARIO_DB_LOCAL, SCENARIO_DB_REMOTE, SCENARIO_DB_NONE])
      self.databaseScenarioCombo.currentIndexChanged.connect(
         self.handleScenarioChange)

      self.databaseTypeCombo = QtWidgets.QComboBox()
      self.databaseTypeCombo.setFixedWidth(200)
      self.databaseTypeCombo.addItems(["Full Database", "Supernode"])
      self.databaseTypeCombo.currentTextChanged.connect(
         self.updateCliCommandDisplay)

      self.ramUsageEdit = QtWidgets.QLineEdit()
      self.ramUsageEdit.setFixedWidth(100)
      self.ramUsageEdit.textChanged.connect(self.updateCliCommandDisplay)

      self.threadCountEdit = QtWidgets.QLineEdit()
      self.threadCountEdit.setFixedWidth(100)
      self.threadCountEdit.textChanged.connect(self.updateCliCommandDisplay)

      self.remoteHostEdit = QtWidgets.QLineEdit()
      self.remoteHostEdit.setFixedWidth(200)
      self.remoteHostEdit.textChanged.connect(self.updateCliCommandDisplay)

      self.remotePortEdit = QtWidgets.QLineEdit()
      self.remotePortEdit.setFixedWidth(100)
      self.remotePortEdit.textChanged.connect(self.updateCliCommandDisplay)

      self.remoteUserEdit = QtWidgets.QLineEdit()
      self.remoteUserEdit.setFixedWidth(200)
      self.remoteUserEdit.textChanged.connect(self.updateCliCommandDisplay)

      self.cliCommandLabel = QtWidgets.QLabel()
      self.cliCommandLabel.setWordWrap(True)
      self.cliCommandLabel.setStyleSheet(
         "background-color: #f0f0f0; border: 1px solid #ccc; padding: 8px; "
         "font-family: monospace; font-size: 10pt; color: #333;")
      self.cliCommandLabel.setText(
         "CLI Command: (will be generated based on settings)")
      self.cliCommandLabel.setMinimumHeight(60)

   def createLocalDatabaseFrame(self):
      """Create the local database configuration frame."""
      self.localDatabaseFrame = QtWidgets.QGroupBox(
         self.tr('Local Database Configuration'))
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

   def createRemoteFrame(self):
      """Create the remote database configuration frame."""
      self.remoteFrame = QtWidgets.QGroupBox(self.tr('Remote Connection'))
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

   def updateCliCommandDisplay(self):
      """Update the CLI command display based on current database settings."""
      dbScenario = self.databaseScenarioCombo.currentText()
      if dbScenario == SCENARIO_DB_LOCAL:
         dbType = self.databaseTypeCombo.currentText()
         ramUsage = self.ramUsageEdit.text() or '4'
         threadCount = self.threadCountEdit.text() or '4'
         dbDir = self.databaseDirEdit.text() or '/path/to/db'
         dbTypeArg = 'DB_SUPER' if dbType == 'Supernode' else 'DB_FULL'
         cmdParts = [
            'ArmoryDB',
            f'--db-type={dbTypeArg}',
            f'--ram-usage={ramUsage}',
            f'--thread-count={threadCount}',
            f'--datadir="{dbDir}"'
         ]
      elif dbScenario == SCENARIO_DB_REMOTE:
         host = self.remoteHostEdit.text() or 'localhost'
         port = self.remotePortEdit.text() or '9001'
         cmdParts = [
            'Armory',
            f'--armorydb-ip={host}',
            f'--armorydb-port={port}'
         ]
      else:
         cmdParts = ['ArmoryDB', '--offline']
      commandLine = ' '.join(cmdParts)
      self.cliCommandLabel.setText(f"CLI Command: {commandLine}")

   def handleScenarioChange(self, index):
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

   def loadSettings(self):
      """Load database tab settings from configuration."""
      self.databaseDirEdit.setText(os.path.normpath(ARMORY_DB_DIR))

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
            self.dbBootstrapLabel.setStyleSheet(
               "color: orange; font-style: italic;")
            layout = self.databaseScenarioCombo.parent().layout()
            if layout:
               layout.addWidget(self.dbBootstrapLabel)

      self.handleScenarioChange(self.databaseScenarioCombo.currentIndex())
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

   def collectSettings(self):
      """Return current database config from UI as a dict."""
      dbScenario = self.databaseScenarioCombo.currentText()
      isLocal = dbScenario == SCENARIO_DB_LOCAL
      isRemote = dbScenario == SCENARIO_DB_REMOTE
      return {
         'dbPath': str(self.databaseDirEdit.text()),
         'scenario': str(self.databaseScenarioCombo.currentText()),
         'typeDisp': str(self.databaseTypeCombo.currentText()) if isLocal \
            else '',
         'remoteHost': str(self.remoteHostEdit.text()) if isRemote else '',
         'remotePort': str(self.remotePortEdit.text()) if isRemote else '',
         'ram': str(self.ramUsageEdit.text()) if isLocal else '',
         'threads': str(self.threadCountEdit.text()) if isLocal else '',
      }

   def validate(self):
      """Validate database tab settings. Returns True if valid."""
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

   def validateDbPath(self):
      """
      Validate database directory exists, create if needed.
      Returns True if valid/created, False on error.
      """
      dbPath = str(self.databaseDirEdit.text())
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
