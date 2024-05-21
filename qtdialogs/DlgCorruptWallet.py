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

import threading

from armoryengine.PyBtcWallet import PyBtcWallet
#from armoryengine.PyBtcWalletRecovery import RECOVERMODE

from qtdialogs.DlgProgress import DlgProgress
from qtdialogs.qtdefines import HLINE, QRichLabel, makeVertFrame
from armorycolors import htmlColor

from qtpy import QtCore, QtWidgets

#################################################################################
class DlgCorruptWallet(DlgProgress):
   def __init__(self, wallet, status, main=None, parent=None, alreadyFailed=True):
      super(DlgProgress, self).__init__(parent, main)

      self.connectDlg()

      self.main = main
      self.walletList = []
      self.logDirs = []

      self.running = 1
      self.status = 1
      self.isFixing = False
      self.needToSubmitLogs = False
#        self.checkMode = RECOVERMODE.NotSet

      self.lock = threading.Lock()
      self.condVar = threading.Condition(self.lock)

      mainLayout = QtWidgets.QVBoxLayout()

      self.connect(self, SIGNAL('UCF'), self.UCF)
      self.connect(self, SIGNAL('Show'), self.show)
      self.connect(self, SIGNAL('Exec'), self.run_lock)
      self.connect(self, SIGNAL('SNP'), self.setNewProgress)
      self.connect(self, SIGNAL('LFW'), self.LFW)
      self.connect(self, SIGNAL('SRD'), self.SRD)

      if alreadyFailed:
         titleStr = self.tr('Wallet Consistency Check Failed!')
      else:
         titleStr = self.tr('Perform Wallet Consistency Check')

      lblDescr = QRichLabel(self.tr(
         '<font color="%1" size=5><b><u>%2</u></b></font> '
         '<br><br>'
         'Armory software now detects and prevents certain kinds of '
         'hardware errors that could lead to problems with your wallet. '
         '<br>').arg(htmlColor('TextWarn'), titleStr))

      lblDescr.setAlignment(QtCore.Qt.AlignCenter)


      if alreadyFailed:
         self.lblFirstMsg = QRichLabel(self.tr(
             'Armory has detected that wallet file <b>Wallet "%1" (%2)</b> '
             'is inconsistent and should be further analyzed to ensure that your '
             'funds are protected. '
             '<br><br>'
             '<font color="%3">This error will pop up every time you start '
             'Armory until the wallet has been analyzed and fixed!</font>').arg(wallet.labelName, wallet.uniqueIDB58, htmlColor('TextWarn')))
      elif isinstance(wallet, PyBtcWallet):
         self.lblFirstMsg = QRichLabel(self.tr(
             'Armory will perform a consistency check on <b>Wallet "%1" (%2)</b> '
             'and determine if any further action is required to keep your funds '
             'protected.  This check is normally performed on startup on all '
             'your wallets, but you can click below to force another '
             'check.').arg(wallet.labelName, wallet.uniqueIDB58))
      else:
         self.lblFirstMsg = QRichLabel('')

      self.QDS = QtWidgets.QDialog()
      self.lblStatus = QtWidgets.QLabel('')
      self.addStatus(wallet, status)
      self.QDSlo = QtWidgets.QVBoxLayout()
      self.QDS.setLayout(self.QDSlo)

      self.QDSlo.addWidget(self.lblFirstMsg)
      self.QDSlo.addWidget(self.lblStatus)

      self.lblStatus.setVisible(False)
      self.lblFirstMsg.setVisible(True)

      saStatus = QtWidgets.QScrollArea()
      saStatus.setWidgetResizable(True)
      saStatus.setWidget(self.QDS)
      saStatus.setMinimumHeight(250)
      saStatus.setMinimumWidth(500)


      layoutButtons = QtWidgets.QGridLayout()
      layoutButtons.setColumnStretch(0, 1)
      layoutButtons.setColumnStretch(4, 1)
      self.btnClose = QtWidgets.QPushButton(self.tr('Hide'))
      self.btnFixWallets = QtWidgets.QPushButton(self.tr('Run Analysis and Recovery Tool'))
      self.btnFixWallets.setDisabled(True)
      self.connect(self.btnFixWallets, SIGNAL('clicked()'), self.doFixWallets)
      self.connect(self.btnClose, SIGNAL('clicked()'), self.hide)
      layoutButtons.addWidget(self.btnClose, 0, 1, 1, 1)
      layoutButtons.addWidget(self.btnFixWallets, 0, 2, 1, 1)

      self.lblDescr2 = QRichLabel('')
      self.lblDescr2.setAlignment(QtCore.Qt.AlignCenter)

      self.lblFixRdy = QRichLabel(self.tr(
         '<u>Your wallets will be ready to fix once the scan is over</u><br> '
         'You can hide this window until then<br>'))

      self.lblFixRdy.setAlignment(QtCore.Qt.AlignCenter)

      self.frmBottomMsg = makeVertFrame(['Space(5)',
                                         HLINE(),
                                         self.lblDescr2,
                                         self.lblFixRdy,
                                         HLINE()])

      self.frmBottomMsg.setVisible(False)


      mainLayout.addWidget(lblDescr)
      mainLayout.addWidget(saStatus)
      mainLayout.addWidget(self.frmBottomMsg)
      mainLayout.addLayout(layoutButtons)

      self.setLayout(mainLayout)
      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)
      self.setWindowTitle(self.tr('Wallet Error'))

   def addStatus(self, wallet, status):
      if wallet:
         strStatus = ''.join(status) + str(self.lblStatus.text())
         self.lblStatus.setText(strStatus)

         self.walletList.append(wallet)

   def show(self):
      super(DlgCorruptWallet, self).show()
      self.activateWindow()

   def run_lock(self):
      self.btnClose.setVisible(False)
      self.hide()
      super(DlgProgress, self).exec_()
      self.walletList = None

   def UpdateCanFix(self, conditions, canFix=False):
      self.emit(SIGNAL('UCF'), conditions, canFix)

   def UCF(self, conditions, canFix=False):
      self.lblFixRdy.setText('')
      if canFix:
         self.btnFixWallets.setEnabled(True)
         self.btnClose.setText(self.tr('Close'))
         self.btnClose.setVisible(False)
         self.connect(self.btnClose, SIGNAL('clicked()'), self.reject)
         self.hide()

   def doFixWallets(self):
      self.lblFixRdy.hide()
      self.adjustSize()

      self.lblStatus.setVisible(True)
      self.lblFirstMsg.setVisible(False)
      self.frmBottomMsg.setVisible(False)

      self.btnClose.setDisabled(True)
      self.btnFixWallets.setDisabled(True)
      self.isFixing = True

      self.lblStatus.hide()
      self.QDSlo.removeWidget(self.lblStatus)

      for wlt in self.walletList:
         self.main.removeWalletFromApplication(wlt.uniqueIDB58)

#        FixWalletList(self.walletList, self, Progress=self.UpdateText, async=True)
      self.adjustSize()

#    def ProcessWallet(self, mode=RECOVERMODE.Full):
   def ProcessWallet(self, mode):
        #Serves as the entry point for non processing wallets that arent loaded
        #or fully processed. Only takes 1 wallet at a time

      if len(self.walletList) > 0:
         wlt = None
         wltPath = ''

         if isinstance(self.walletList[0], str):
            wltPath = self.walletList[0]
         else:
            wlt = self.walletList[0]

      self.lblDesc = QtWidgets.QLabel('')
      self.QDSlo.addWidget(self.lblDesc)

      self.lblFixRdy.hide()
      self.adjustSize()

      self.frmBottomMsg.setVisible(False)
      self.lblStatus.setVisible(True)
      self.lblFirstMsg.setVisible(False)

      self.btnClose.setDisabled(True)
      self.btnFixWallets.setDisabled(True)
      self.isFixing = True

#        self.checkMode = mode
#        ParseWallet(wltPath, wlt, mode, self,
#                               Progress=self.UpdateText, async=True)

   def UpdateDlg(self, text=None, HBar=None, Title=None):
      if text is not None: self.lblDesc.setText(text)
      self.adjustSize()

   def accept(self):
      self.main.emit(SIGNAL('checkForNegImports'))
      super(DlgCorruptWallet, self).accept()

   def reject(self):
      if not self.isFixing:
         super(DlgProgress, self).reject()
         self.main.emit(SIGNAL('checkForNegImports'))

   def sigSetNewProgress(self, status):
      self.emit(SIGNAL('SNP'), status)

   def setNewProgress(self, status):
      self.lblDesc = QtWidgets.QLabel('')
      self.QDSlo.addWidget(self.lblDesc)
        #self.QDS.adjustSize()
      status[0] = 1

   def setRecoveryDone(self, badWallets, goodWallets, fixedWallets, fixers):
      self.emit(SIGNAL('SRD'), badWallets, goodWallets, fixedWallets, fixers)

   def SRD(self, badWallets, goodWallets, fixedWallets, fixerObjs):
      self.btnClose.setEnabled(True)
      self.btnClose.setVisible(True)
      self.btnClose.setText(self.tr('Continue'))
      self.btnFixWallets.setVisible(False)
      self.btnClose.disconnect(self, SIGNAL('clicked()'), self.hide)
      self.btnClose.connect(self, SIGNAL('clicked()'), self.accept)
      self.isFixing = False
      self.frmBottomMsg.setVisible(True)

      anyNegImports = False
      for fixer in fixerObjs:
         if len(fixer.negativeImports) > 0:
            anyNegImports = True
            break


      if len(badWallets) > 0:
         self.lblDescr2.setText(self.tr(
            '<font size=4 color="%1"><b>Failed to fix wallets!</b></font>').arg(htmlColor('TextWarn')))
         self.main.statusBar().showMessage('Failed to fix wallets!', 150000)
      elif len(goodWallets) == len(fixedWallets) and not anyNegImports:
         self.lblDescr2.setText(self.tr(
            '<font size=4 color="%1"><b>Wallet(s) consistent, nothing to '
            'fix.</b></font>', "", len(goodWallets)).arg(htmlColor("TextBlue")))
         self.main.statusBar().showMessage( \
             self.tr("Wallet(s) consistent!", "", len(goodWallets)) % \
             15000)
      elif len(fixedWallets) > 0 or anyNegImports:
#            if self.checkMode != RECOVERMODE.Check:
#                self.lblDescr2.setText(self.tr(
#                   '<font color="%1"><b> '
#                   '<font size=4><b><u>There may still be issues with your '
#                   'wallet!</u></b></font> '
#                   '<br>'
#                   'It is important that you send us the recovery logs '
#                   'and an email address so the Armory team can check for '
#                   'further risk to your funds!</b></font>').arg(htmlColor('TextWarn')))
#                #self.main.statusBar().showMessage('Wallets fixed!', 15000)
#            else:
#                self.lblDescr2.setText(self.tr('<h2 style="color: red;"> \
#                                        Consistency check failed! </h2>'))

         self.lblDescr2.setText(self.tr('<h2 style="color: red;"> \
                                  Consistency check failed! </h2>'))
      self.adjustSize()


   def loadFixedWallets(self, wallets):
      self.emit(SIGNAL('LFW'), wallets)

   def LFW(self, wallets):
      for wlt in wallets:
         newWallet = PyBtcWallet().readWalletFile(wlt)
         self.main.addWalletToApplication(newWallet, False)

      self.main.emit(SIGNAL('checkForkedImport'))


    # Decided that we can just add all the logic to
    #def checkForkedSubmitLogs(self):
        #forkedImports = []
        #for wlt in self.walletMap:
            #if self.walletMap[wlt].hasForkedImports:
                #dlgIWR = DlgInconsistentWltReport(self, self.main, self.logDirs)
                #if dlgIWR.exec_():
                #return
            #return