##############################################################################
#                                                                            #
#  Copyright (C) 2025, goatpig                                               #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

import os
from qtpy import QtCore, QtGui, QtWidgets
from qtdialogs.qtdefines import QRichLabel, apply_dialog_base_style, UI_STYLE_FRAME, UI_STYLE_BUTTON_STANDARD
from armorycolors import htmlColor

# --- Constants for UI layout ---
DIALOG_MARGIN = 24
CONTENT_SPACING = 8
FRAME_PADDING = 8
LOGO_SPACING = 16
MINIMUM_WIDTH = 680
MINIMUM_HEIGHT = 450

# System requirements
MIN_DISK_SPACE_GB = 10
MIN_RAM_GB = 2
MIN_CPU_CORES = 2

################################################################################
def createStyledLabel(text, alignment=QtCore.Qt.AlignLeft):
    """Creates a styled QRichLabel with consistent formatting."""
    label = QRichLabel(text)
    label.setAlignment(alignment)
    return label

def createRequirementLabel():
    """Creates an empty label for system requirements display."""
    return QRichLabel("")

def createStyledButton(text, style):
    """Creates a styled button with consistent formatting."""
    button = QtWidgets.QPushButton(text)
    button.setStyleSheet(style)
    return button

def createContentFrame(style):
    """Creates a styled frame for content display."""
    frame = QtWidgets.QFrame()
    frame.setFrameStyle(QtWidgets.QFrame.NoFrame)
    frame.setStyleSheet(style)
    return frame

################################################################################

# Consolidated styles for UI elements (use theme)
REQUIREMENTS_FRAME_STYLE = UI_STYLE_FRAME

################################################################################
class DlgWelcomeScreen(QtWidgets.QDialog):
    def __init__(self, parent=None):
        """Initialize the welcome screen dialog."""
        super().__init__(parent)
        
        self.initMemberVariables()
        self.setupDialogProperties()
        self.createMainLayout()
        self.checkAndUpdateSystemRequirements()

    def initMemberVariables(self):
        """Initialize all member variables with default values."""
        # System requirement labels
        self.diskLabel = None
        self.ramLabel = None
        self.cpuLabel = None
        self.getStartedBtn = None

    def setupDialogProperties(self):
        """Configure basic dialog properties and styling."""
        self.setWindowTitle('Welcome to Armory')
        apply_dialog_base_style(self)
        self.setMinimumWidth(MINIMUM_WIDTH)
        self.setMinimumHeight(MINIMUM_HEIGHT)

    def createMainLayout(self):
        """Set up the main layout with all content sections."""
        mainLayout = QtWidgets.QGridLayout()
        mainLayout.setContentsMargins(DIALOG_MARGIN, DIALOG_MARGIN, DIALOG_MARGIN, DIALOG_MARGIN)
        mainLayout.setSpacing(CONTENT_SPACING)

        # Create main content widget
        contentWidget = self.createContentWidget()
        mainLayout.addWidget(contentWidget, 0, 0, 1, 1)
        
        self.setLayout(mainLayout)

    def createContentWidget(self):
        """Create the main content widget containing all sections."""
        contentWidget = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(contentWidget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(CONTENT_SPACING)

        # Add all content sections
        layout.addWidget(self.createLogoSection())
        layout.addSpacing(LOGO_SPACING)
        layout.addWidget(self.createIntroSection())
        layout.addWidget(self.createStepsSection())
        layout.addWidget(self.createRequirementsSection())
        layout.addWidget(self.createButtonSection())
        layout.addWidget(self.createVersionSection())

        return contentWidget

    def createLogoSection(self):
        """Create the Armory logo section."""
        logoLabel = QtWidgets.QLabel()
        logoPixmap = QtGui.QPixmap('./img/armory_logo_white_text_h56.png')
        logoLabel.setPixmap(logoPixmap)
        logoLabel.setAlignment(QtCore.Qt.AlignCenter)
        return logoLabel

    def createIntroSection(self):
        """Create the introduction text section."""
        introText = createStyledLabel("""
            <p style="color: %s; font-size: 10pt; margin: 0;">
            Thank you for choosing Armory as your Bitcoin wallet. Before you can start using 
            Armory, we need to configure a few essential settings. This setup process will 
            guide you through:
            </p>
        """ % htmlColor('Foreground'))
        return introText

    def createStepsSection(self):
        """Create the setup steps description section."""
        stepsText = createStyledLabel("""
            <div style="color: %s; font-size: 10pt;">
            • <b>Wallet Settings:</b> Choose where to store your wallet data and manage any existing wallets<br>
            • <b>Core Settings:</b> Configure your Bitcoin Core connection for blockchain access<br>
            • <b>Database Settings:</b> Set up how Armory will store and manage transaction data
            </div>
        """ % htmlColor('Foreground'))
        return stepsText

    def createRequirementsSection(self):
        """Create the system requirements check section."""
        # Container widget for the entire requirements section
        container = QtWidgets.QWidget()
        containerLayout = QtWidgets.QVBoxLayout(container)
        containerLayout.setContentsMargins(0, 0, 0, 0)
        containerLayout.setSpacing(CONTENT_SPACING)

        # Requirements header
        reqHeader = createStyledLabel(
            '<span style="color: %s; font-size: 10pt;"><b>System Requirements Check:</b></span>' % htmlColor('TextYellow'))
        containerLayout.addWidget(reqHeader)

        # Requirements frame
        reqFrame = createContentFrame(REQUIREMENTS_FRAME_STYLE)
        reqLayout = QtWidgets.QVBoxLayout()
        reqLayout.setSpacing(CONTENT_SPACING)
        reqLayout.setContentsMargins(FRAME_PADDING, FRAME_PADDING, FRAME_PADDING, FRAME_PADDING)

        # Create requirement labels
        self.diskLabel = createRequirementLabel()
        self.ramLabel = createRequirementLabel()
        self.cpuLabel = createRequirementLabel()

        reqLayout.addWidget(self.diskLabel)
        reqLayout.addWidget(self.ramLabel)
        reqLayout.addWidget(self.cpuLabel)

        reqFrame.setLayout(reqLayout)
        containerLayout.addWidget(reqFrame)

        return container

    def createButtonSection(self):
        """Create the button section with Get Started button."""
        btnContainer = QtWidgets.QWidget()
        btnLayout = QtWidgets.QHBoxLayout(btnContainer)
        btnLayout.setContentsMargins(0, 0, 0, 0)

        # Get Started Button
        self.getStartedBtn = createStyledButton('Get Started', UI_STYLE_BUTTON_STANDARD)
        self.getStartedBtn.clicked.connect(self.accept)

        btnLayout.addStretch()
        btnLayout.addWidget(self.getStartedBtn)
        btnLayout.addStretch()

        return btnContainer

    def createVersionSection(self):
        """Create the version and copyright information section."""
        versionText = createStyledLabel("""
            <div style="color: %s; font-size: 8pt; text-align: center; line-height: 1.2;">
            Armory Bitcoin Wallet: Version 0.96.00-beta<br>
            © 2025 Goatpig
            </div>
        """ % htmlColor('Foreground'), QtCore.Qt.AlignCenter)
        return versionText

    def updateSystemRequirements(self, requirements):
        """Update the system requirements display with provided values."""
        def createStatusText(requirement, name):
            """Create styled status text for a requirement."""
            color = htmlColor('TextGreen') if requirement else htmlColor('TextRed')
            status = 'Sufficient' if requirement else 'Insufficient'
            return f'<div style="line-height: 1.2;">' \
                   f'<span style="color: {color}; font-size: 10pt;">✓ {name}: {status}</span>' \
                   f'</div>'

        # Update system requirement displays
        self.diskLabel.setText(createStatusText(
            requirements['disk_space'], 'Storage Space'))
        self.ramLabel.setText(createStatusText(
            requirements['ram'], 'System Memory'))
        self.cpuLabel.setText(createStatusText(
            requirements['cpu_cores'], 'CPU Cores'))

    def checkSystemRequirements(self):
        """Check if the system meets minimum requirements for running Armory."""
        requirements = {
            'disk_space': False,
            'ram': False,
            'cpu_cores': False
        }

        try:
            requirements['disk_space'] = self._checkDiskSpace()
            requirements['ram'] = self._checkSystemMemory()
            requirements['cpu_cores'] = self._checkCpuCores()
        except Exception as e:
            print(f"Error checking system requirements: {str(e)}")
            # If we can't check requirements, assume they're sufficient
            return self._getDefaultRequirements()

        return requirements

    def _checkDiskSpace(self):
        """Check if sufficient disk space is available."""
        try:
            if os.name == 'nt':  # Windows
                return self._checkDiskSpaceWindows()
            else:  # Unix-like systems
                return self._checkDiskSpaceUnix()
        except Exception:
            return True  # Assume sufficient if check fails

    def _checkDiskSpaceWindows(self):
        """Check disk space on Windows using Windows API."""
        import ctypes
        free_bytes = ctypes.c_ulonglong(0)
        ctypes.windll.kernel32.GetDiskFreeSpaceExW(
            ctypes.c_wchar_p(os.path.expanduser('~')),
            None, None, ctypes.pointer(free_bytes))
        free_gb = free_bytes.value / (1024**3)
        return free_gb >= MIN_DISK_SPACE_GB

    def _checkDiskSpaceUnix(self):
        """Check disk space on Unix-like systems."""
        try:
            st = os.statvfs(os.path.expanduser('~'))
            free_gb = (st.f_bavail * st.f_frsize) / (1024**3)
        except AttributeError:
            # If statvfs is not available, try alternative method
            import shutil
            free_gb = shutil.disk_usage(os.path.expanduser('~')).free / (1024**3)
        return free_gb >= MIN_DISK_SPACE_GB

    def _checkSystemMemory(self):
        """Check if sufficient system memory is available."""
        try:
            # Try psutil first (most reliable)
            import psutil
            total_ram = psutil.virtual_memory().total / (1024**3)
            return total_ram >= MIN_RAM_GB
        except ImportError:
            # Fallback to platform-specific methods
            if os.name == 'nt':
                return self._checkMemoryWindows()
            else:
                return self._checkMemoryUnix()

    def _checkMemoryWindows(self):
        """Check system memory on Windows using Windows API."""
        try:
            import ctypes
            kernel32 = ctypes.windll.kernel32
            c_ulonglong = ctypes.c_ulonglong
            
            class MEMORYSTATUSEX(ctypes.Structure):
                _fields_ = [
                    ('dwLength', ctypes.c_ulong),
                    ('dwMemoryLoad', ctypes.c_ulong),
                    ('ullTotalPhys', c_ulonglong),
                    ('ullAvailPhys', c_ulonglong),
                    ('ullTotalPageFile', c_ulonglong),
                    ('ullAvailPageFile', c_ulonglong),
                    ('ullTotalVirtual', c_ulonglong),
                    ('ullAvailVirtual', c_ulonglong),
                    ('ullAvailExtendedVirtual', c_ulonglong),
                ]

            stat = MEMORYSTATUSEX()
            stat.dwLength = ctypes.sizeof(stat)
            kernel32.GlobalMemoryStatusEx(ctypes.byref(stat))
            total_ram = stat.ullTotalPhys / (1024**3)
            return total_ram >= MIN_RAM_GB
        except Exception:
            return True  # Assume sufficient if check fails

    def _checkMemoryUnix(self):
        """Check system memory on Unix-like systems."""
        try:
            with open('/proc/meminfo', 'r') as f:
                for line in f:
                    if line.startswith('MemTotal:'):
                        total_ram = int(line.split()[1]) / (1024**2)  # Convert KB to GB
                        return total_ram >= MIN_RAM_GB
            return True  # Assume sufficient if not found
        except Exception:
            return True  # Assume sufficient if check fails

    def _checkCpuCores(self):
        """Check if sufficient CPU cores are available."""
        try:
            import multiprocessing
            cpu_count = multiprocessing.cpu_count()
            return cpu_count >= MIN_CPU_CORES
        except Exception:
            return True  # Assume sufficient if check fails

    def _getDefaultRequirements(self):
        """Return default requirements assuming all are sufficient."""
        return {
            'disk_space': True,
            'ram': True,
            'cpu_cores': True
        }

    def checkAndUpdateSystemRequirements(self):
        """Check system requirements and update the display."""
        requirements = self.checkSystemRequirements()
        self.updateSystemRequirements(requirements)

    def show(self):
        """Override show to update system requirements before displaying."""
        self.checkAndUpdateSystemRequirements()
        super().show()

    @staticmethod
    def showWelcome(parent=None):
        """Static method to show the welcome screen.
        
        Returns:
            bool: True if user clicked Get Started, False otherwise
        """
        dlg = DlgWelcomeScreen(parent)
        result = dlg.exec_()
        return result == QtWidgets.QDialog.Accepted
