##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2024, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################
import os

from qtpy import QtCore, QtWidgets

from armorycolors import htmlColor
from armoryengine.ArmoryUtils import ARMORY_HOME_DIR, LOGINFO, OS_MACOSX
#from armoryengine.PyBtcWalletRecovery import RECOVERMODE
from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgCorruptWallet import DlgCorruptWallet
from qtdialogs.DlgWalletSelect import DlgWalletSelect
from qtdialogs.qtdefines import GETFONT, QRichLabel, STYLE_SUNKEN, \
   makeHorizFrame, tightSizeNChar

###############################################################################
class DlgWltRecoverWallet(ArmoryDialog):
   def __init__(self, parent=None, main=None):
      super(DlgWltRecoverWallet, self).__init__(parent, main)

      self.edtWalletPath = QtWidgets.QLineEdit()
      self.edtWalletPath.setFont(GETFONT('Fixed', 9))
      edtW,edtH = tightSizeNChar(self.edtWalletPath, 50)
      self.edtWalletPath.setMinimumWidth(edtW)
      self.btnWalletPath = QtWidgets.QPushButton(self.tr('Browse File System'))

      self.btnWalletPath.clicked.connect(self.selectFile)

      lblDesc = QRichLabel(self.tr(
         '<b>Wallet Recovery Tool: '
         '</b><br>'
         'This tool will recover data from damaged or inconsistent '
         'wallets.  Specify a wallet file and Armory will analyze the '
         'wallet and fix any errors with it. '
         '<br><br>'
         '<font color="%s">If any problems are found with the specified '
         'wallet, Armory will provide explanation and instructions to '
         'transition to a new wallet.' % htmlColor('TextWarn')))
      lblDesc.setScaledContents(True)

      lblWalletPath = QRichLabel(self.tr('Wallet Path:'))

      self.selectedWltID = None

      def doWltSelect():
         dlg = DlgWalletSelect(self, self.main, self.tr('Select Wallet...'), '')
         if dlg.exec_():
            self.selectedWltID = dlg.selectedID
            wlt = self.parent.walletMap[dlg.selectedID]
            self.edtWalletPath.setText(wlt.walletPath)

      self.btnWltSelect = QtWidgets.QPushButton(self.tr("Select Loaded Wallet"))
      self.btnWltSelect.clicked.connect(doWltSelect)

      layoutMgmt = QtWidgets.QGridLayout()
      wltSltQF = QtWidgets.QFrame()
      wltSltQF.setFrameStyle(STYLE_SUNKEN)

      layoutWltSelect = QtWidgets.QGridLayout()
      layoutWltSelect.addWidget(lblWalletPath,      0,0, 1, 1)
      layoutWltSelect.addWidget(self.edtWalletPath, 0,1, 1, 3)
      layoutWltSelect.addWidget(self.btnWltSelect,  1,0, 1, 2)
      layoutWltSelect.addWidget(self.btnWalletPath, 1,2, 1, 2)
      layoutWltSelect.setColumnStretch(0, 0)
      layoutWltSelect.setColumnStretch(1, 1)
      layoutWltSelect.setColumnStretch(2, 1)
      layoutWltSelect.setColumnStretch(3, 0)

      wltSltQF.setLayout(layoutWltSelect)

      layoutMgmt.addWidget(makeHorizFrame([lblDesc], STYLE_SUNKEN), 0,0, 2,4)
      layoutMgmt.addWidget(wltSltQF, 2, 0, 3, 4)

      self.rdbtnStripped = QtWidgets.QRadioButton('', parent=self)
      self.rdbtnStripped.event.connect(self.rdClicked)
      lblStripped = QtWidgets.QLabel(self.tr('<b>Stripped Recovery</b><br>Only attempts to \
                            recover the wallet\'s rootkey and chaincode'))
      layout_StrippedH = QtWidgets.QGridLayout()
      layout_StrippedH.addWidget(self.rdbtnStripped, 0, 0, 1, 1)
      layout_StrippedH.addWidget(lblStripped, 0, 1, 2, 19)

      self.rdbtnBare = QtWidgets.QRadioButton('')
      lblBare = QtWidgets.QLabel(self.tr('<b>Bare Recovery</b><br>Attempts to recover all private key related data'))
      layout_BareH = QtWidgets.QGridLayout()
      layout_BareH.addWidget(self.rdbtnBare, 0, 0, 1, 1)
      layout_BareH.addWidget(lblBare, 0, 1, 2, 19)

      self.rdbtnFull = QtWidgets.QRadioButton('')
      self.rdbtnFull.setChecked(True)
      lblFull = QtWidgets.QLabel(self.tr('<b>Full Recovery</b><br>Attempts to recover as much data as possible'))
      layout_FullH = QtWidgets.QGridLayout()
      layout_FullH.addWidget(self.rdbtnFull, 0, 0, 1, 1)
      layout_FullH.addWidget(lblFull, 0, 1, 2, 19)

      self.rdbtnCheck = QtWidgets.QRadioButton('')
      lblCheck = QtWidgets.QLabel(self.tr('<b>Consistency Check</b><br>Checks wallet consistency. Works with both full and watch only<br> wallets.'
                         ' Unlocking of encrypted wallets is not mandatory'))
      layout_CheckH = QtWidgets.QGridLayout()
      layout_CheckH.addWidget(self.rdbtnCheck, 0, 0, 1, 1)
      layout_CheckH.addWidget(lblCheck, 0, 1, 3, 19)


      layoutMode = QtWidgets.QGridLayout()
      layoutMode.addLayout(layout_StrippedH, 0, 0, 2, 4)
      layoutMode.addLayout(layout_BareH, 2, 0, 2, 4)
      layoutMode.addLayout(layout_FullH, 4, 0, 2, 4)
      layoutMode.addLayout(layout_CheckH, 6, 0, 3, 4)


        #self.rdnGroup = QtWidgets.QButtonGroup()
        #self.rdnGroup.addButton(self.rdbtnStripped)
        #self.rdnGroup.addButton(self.rdbtnBare)
        #self.rdnGroup.addButton(self.rdbtnFull)
        #self.rdnGroup.addButton(self.rdbtnCheck)


      layoutMgmt.addLayout(layoutMode, 5, 0, 9, 4)
      """
      wltModeQF = QtWidgets.QFrame()
      wltModeQF.setFrameStyle(STYLE_SUNKEN)
      wltModeQF.setLayout(layoutMode)

      layoutMgmt.addWidget(wltModeQF, 5, 0, 9, 4)
      wltModeQF.setVisible(False)


      btnShowAllOpts = QLabelButton(self.tr("All Recovery Options>>>"))
      frmBtn = makeHorizFrame(['Stretch', btnShowAllOpts, 'Stretch'], STYLE_SUNKEN)
      layoutMgmt.addWidget(frmBtn, 5, 0, 9, 4)

      def expandOpts():
         wltModeQF.setVisible(True)
         btnShowAllOpts.setVisible(False)
      btnShowAllOpts.clicked.connect(expandOpts)

      if not self.main.usermode==USERMODE.Expert:
         frmBtn.setVisible(False)
      """

      self.btnRecover = QtWidgets.QPushButton(self.tr('Recover'))
      self.btnCancel  = QtWidgets.QPushButton(self.tr('Cancel'))
      layout_btnH = QtWidgets.QHBoxLayout()
      layout_btnH.addWidget(self.btnRecover, 1)
      layout_btnH.addWidget(self.btnCancel, 1)

      def updateBtn(qstr):
         if os.path.exists(str(qstr).strip()):
            self.btnRecover.setEnabled(True)
            self.btnRecover.setToolTip('')
         else:
            self.btnRecover.setEnabled(False)
            self.btnRecover.setToolTip(self.tr('The entered path does not exist'))

      updateBtn('')
      self.edtWalletPath.textChanged.connect(updateBtn)


      layoutMgmt.addLayout(layout_btnH, 14, 1, 1, 2)

      self.btnRecover.clicked.connect(self.accept)
      self.btnCancel.clicked.connect(self.reject)

      self.setLayout(layoutMgmt)
      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)
      self.setWindowTitle(self.tr('Wallet Recovery Tool'))
      self.setMinimumWidth(550)

   def rdClicked(self):
        # TODO:  Why does this do nohting?  Was it a stub that was forgotten?
      LOGINFO("clicked")

   def promptWalletRecovery(self):
      """
        Prompts the user with a window asking for wallet path and recovery mode.
        Proceeds to Recover the wallet. Prompt for password if the wallet is locked
        """
#        if self.exec_():
#            path = str(self.edtWalletPath.text())
#            mode = RECOVERMODE.Bare
#            if self.rdbtnStripped.isChecked():
#                mode = RECOVERMODE.Stripped
#            elif self.rdbtnFull.isChecked():
#                mode = RECOVERMODE.Full
#            elif self.rdbtnCheck.isChecked():
#                mode = RECOVERMODE.Check
#
#            if mode==RECOVERMODE.Full and self.selectedWltID:
#                # Funnel all standard, full recovery operations through the
#                # inconsistent-wallet-dialog.
#                wlt = self.main.walletMap[self.selectedWltID]
#                dlgRecoveryUI = DlgCorruptWallet(wlt, [], self.main, self, False)
#                dlgRecoveryUI.exec_(dlgRecoveryUI.doFixWallets())
#            else:
#                # This is goatpig's original behavior - preserved for any
#                # non-loaded wallets or non-full recovery operations.
#                if self.selectedWltID:
#                    wlt = self.main.walletMap[self.selectedWltID]
#                else:
#                    wlt = path
#
#                dlgRecoveryUI = DlgCorruptWallet(wlt, [], self.main, self, False)
#                dlgRecoveryUI.exec_(dlgRecoveryUI.ProcessWallet(mode))
#        else:
#            return False
      return False

   def selectFile(self):
        # Had to reimplement the path selection here, because the way this was
        # implemented doesn't let me access self.main.getFileLoad
      ftypes = self.tr('Wallet files (*.wallet);; All files (*)')
      if not OS_MACOSX:
         pathSelect = str(QtWidgets.QFileDialog.getOpenFileName(self, \
                                  self.tr('Recover Wallet'), \
                                  ARMORY_HOME_DIR, \
                                  ftypes))
      else:
         pathSelect = str(QtWidgets.QFileDialog.getOpenFileName(self, \
                                  self.tr('Recover Wallet'), \
                                  ARMORY_HOME_DIR, \
                                  ftypes, \
                                  options=QtWidgets.QFileDialog.DontUseNativeDialog))

      self.edtWalletPath.setText(pathSelect)