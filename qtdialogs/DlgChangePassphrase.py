#################################################################################
#                                                                               #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                            #
# Distributed under the GNU Affero General Public License (AGPL v3)             #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                          #
#                                                                               #
# Copyright (C) 2016-2025, goatpig                                              #
#  Distributed under the MIT license                                            #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                       #
#                                                                               #
#################################################################################

from qtpy import QtCore, QtGui, QtWidgets

from armorycolors import htmlColor
from armoryengine.ArmoryUtils import isASCII
from armoryengine.CppBridge import BridgePrompt
from armoryengine.BDM import TheBDM

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgPasswd3 import DlgPasswd3
from qtdialogs.qtdefines import MIN_PASSWD_WIDTH
from ui.QtExecuteSignal import TheSignalExecution

################################################################################
class DlgChangePassphrase(ArmoryDialog, BridgePrompt):
   def __init__(self, parent=None, main=None, wallet=None):
      ArmoryDialog.__init__(self, parent, main)
      BridgePrompt.__init__(self)
      self.wallet = wallet
      self.passphrase = None

      layout = QtWidgets.QGridLayout()

      #progress frame
      self.progressLabel = QtWidgets.QLabel("place holder")
      self.progressLabel.setFrameStyle(QtWidgets.QFrame.Panel | QtWidgets.QFrame.Sunken)
      self.progressLabel.setAlignment(QtCore.Qt.AlignCenter)
      self.progressLabel.setVisible(False)
      layout.addWidget(self.progressLabel, 0, 0, 1, 2)

      #legacy change passphrase GUI
      if not self.wallet or not self.wallet.useEncryption:
         lblDlgDescr = QtWidgets.QLabel(self.tr(
            'Please enter an passphrase for wallet encryption.\n\n'
            'A good passphrase consists of at least 8 or more\n'
            'random letters, or 5 or more random words.\n'))
         lblDlgDescr.setWordWrap(True)
         layout.addWidget(lblDlgDescr, 1, 0, 1, 2)
      else:
         lblDlgDescr = QtWidgets.QLabel(self.tr(
            "Change your wallet encryption passphrase"))
         layout.addWidget(lblDlgDescr, 1, 0, 1, 2)
         self.edtPasswdOrig = QtWidgets.QLineEdit()
         self.edtPasswdOrig.setEchoMode(QtWidgets.QLineEdit.Password)
         self.edtPasswdOrig.setMinimumWidth(MIN_PASSWD_WIDTH(self))
         lblCurrPasswd = QtWidgets.QLabel(self.tr('Current Passphrase:'))
         layout.addWidget(lblCurrPasswd, 2, 0)
         layout.addWidget(self.edtPasswdOrig, 2, 1)

      lblPwd1 = QtWidgets.QLabel(self.tr("New Passphrase:"))
      self.edtPasswd1 = QtWidgets.QLineEdit()
      self.edtPasswd1.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd1.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      lblPwd2 = QtWidgets.QLabel(self.tr("Again:"))
      self.edtPasswd2 = QtWidgets.QLineEdit()
      self.edtPasswd2.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd2.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      layout.addWidget(lblPwd1, 3, 0)
      layout.addWidget(lblPwd2, 4, 0)
      layout.addWidget(self.edtPasswd1, 3, 1)
      layout.addWidget(self.edtPasswd2, 4, 1)

      self.lblMatches = QtWidgets.QLabel(' ' * 20)
      self.lblMatches.setTextFormat(QtCore.Qt.RichText)
      layout.addWidget(self.lblMatches, 5, 1)

      self.chkDisableCrypt = QtWidgets.QCheckBox(self.tr(
         'Disable encryption for this wallet'))
      if self.wallet and self.wallet.useEncryption:
         self.chkDisableCrypt.toggled.connect(self.disablePassphraseBoxes)
         layout.addWidget(self.chkDisableCrypt, 5, 0)

      self.btnAccept = QtWidgets.QPushButton(self.tr("Accept"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      layout.addWidget(buttonBox, 6, 0, 1, 2)

      if not self.wallet or not self.wallet.useEncryption:
         self.setWindowTitle(self.tr("Set Encryption Passphrase"))
      else:
         self.setWindowTitle(self.tr("Change Encryption Passphrase"))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))
      self.setLayout(layout)

      self.edtPasswd1.textChanged.connect(self.checkPassphrase)
      self.edtPasswd2.textChanged.connect(self.checkPassphrase)
      self.btnAccept.clicked.connect(self.checkPassphraseFinal)
      self.btnCancel.clicked.connect(self.reject)

   def disablePassphraseBoxes(self, noEncrypt=True):
      self.edtPasswd1.setEnabled(not noEncrypt)
      self.edtPasswd2.setEnabled(not noEncrypt)

   def checkPassphrase(self):
      if self.chkDisableCrypt.isChecked():
         return True
      p1 = self.edtPasswd1.text()
      p2 = self.edtPasswd2.text()
      goodColor = htmlColor('TextGreen')
      badColor = htmlColor('TextRed')
      if not isASCII(p1) or not isASCII(p2):
         self.lblMatches.setText(self.tr(
            '<font color=%s><b>Passphrase is non-ASCII!</b></font>' % badColor))
         return False
      if not p1 == p2:
         self.lblMatches.setText(self.tr(
            '<font color=%s><b>Passphrases do not match!</b></font>' % badColor))
         return False
      if len(p1) < 5:
         self.lblMatches.setText(self.tr(
            '<font color=%s><b>Passphrase is too short!</b></font>' % badColor))
         return False
      self.lblMatches.setText(self.tr(
         '<font color=%s><b>Passphrases match!</b></font>' % goodColor))
      return True

   def checkPassphraseFinal(self):
      if not self.chkDisableCrypt.isChecked():
         if self.checkPassphrase():
            dlg = DlgPasswd3(self, self.main)
            if dlg.exec_():
               if not str(dlg.edtPasswd3.text()) == str(self.edtPasswd1.text()):
                  QtWidgets.QMessageBox.critical(self,
                     self.tr('Invalid Passphrase'),
                     self.tr('You entered your confirmation passphrase incorrectly!'),
                     QtWidgets.QMessageBox.Ok)
            else:
               self.reject()
               return
      if not self.wallet:
         self.accept()
         return

      if self.wallet.useEncryption:
         self.passphrase = str(self.edtPasswdOrig.text())

      def callback(packet):
         if packet.success == False:
            QtWidgets.QMessageBox.critical(self, self.tr('Invalid Passphrase'),
               self.tr('Previous passphrase is not correct!  Could not unlock wallet.'),
               QtWidgets.QMessageBox.Ok
            )
         else:
            QtWidgets.QMessageBox.information(self, self.tr('Success'),
               self.tr('Passphrase was changed successfully!'),
               QtWidgets.QMessageBox.Ok
            )
            self.accept()
      def callbackWrapper(packet):
         TheSignalExecution.executeMethod(callback, packet)
      self.wallet.changePassphrase(True, self.promptId, callbackWrapper)

   def updateProgress(self, verbose: str):
      def innerMethod(message):
         self.progressLabel.setText(message)
         if not self.progressLabel.isVisible():
            self.progressLabel.setVisible(True)
      TheSignalExecution.executeMethod(innerMethod, verbose)

   def parseProtoPacket(self, payload):
      if payload.which() == 'cleanup':
         self.updateProgress("done")
         TheBDM.unregisterPrompt(self.promptId)

      elif payload.which() == 'unlockRequest':
         packet = self.getNewPacket()
         if self.passphrase:
            self.updateProgress("unlocking wallet")
            packet.success = True
            packet.unlockRequest = self.passphrase
            self.passphrase = None
         else:
            packet.success = False
         self.reply()

      elif payload.which() == 'setPassphrase':
         packet = self.getNewPacket()
         if self.chkDisableCrypt.isChecked():
            packet.success = False
         else:
            self.updateProgress("encrypting wallet")
            packet.success = True
            setPassReply = packet.init('setPassphrase')
            setPassReply.passphrase = str(self.edtPasswd1.text())

            if not self.wallet.useEncryption:
               setPassReply.kdfTargetMs = 2000
            else:
               setPassReply.reuseKdf = True
         self.reply()
