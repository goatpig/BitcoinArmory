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

from qtpy import QtCore, QtGui, QtWidgets

from armorycolors import htmlColor
from armoryengine.ArmoryUtils import isASCII

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgPasswd3 import DlgPasswd3
from qtdialogs.qtdefines import MIN_PASSWD_WIDTH

################################################################################
class DlgChangePassphrase(ArmoryDialog):
   def __init__(self, parent=None, main=None, noPrevEncrypt=True):
      super(DlgChangePassphrase, self).__init__(parent, main)



      layout = QtWidgets.QGridLayout()
      if noPrevEncrypt:
         lblDlgDescr = QtWidgets.QLabel(self.tr('Please enter an passphrase for wallet encryption.\n\n'
                              'A good passphrase consists of at least 8 or more\n'
                              'random letters, or 5 or more random words.\n'))
         lblDlgDescr.setWordWrap(True)
         layout.addWidget(lblDlgDescr, 0, 0, 1, 2)
      else:
         lblDlgDescr = QtWidgets.QLabel(self.tr("Change your wallet encryption passphrase"))
         layout.addWidget(lblDlgDescr, 0, 0, 1, 2)
         self.edtPasswdOrig = QtWidgets.QLineEdit()
         self.edtPasswdOrig.setEchoMode(QtWidgets.QLineEdit.Password)
         self.edtPasswdOrig.setMinimumWidth(MIN_PASSWD_WIDTH(self))
         lblCurrPasswd = QtWidgets.QLabel(self.tr('Current Passphrase:'))
         layout.addWidget(lblCurrPasswd, 1, 0)
         layout.addWidget(self.edtPasswdOrig, 1, 1)



      lblPwd1 = QtWidgets.QLabel(self.tr("New Passphrase:"))
      self.edtPasswd1 = QtWidgets.QLineEdit()
      self.edtPasswd1.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd1.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      lblPwd2 = QtWidgets.QLabel(self.tr("Again:"))
      self.edtPasswd2 = QtWidgets.QLineEdit()
      self.edtPasswd2.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd2.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      layout.addWidget(lblPwd1, 2, 0)
      layout.addWidget(lblPwd2, 3, 0)
      layout.addWidget(self.edtPasswd1, 2, 1)
      layout.addWidget(self.edtPasswd2, 3, 1)

      self.lblMatches = QtWidgets.QLabel(' ' * 20)
      self.lblMatches.setTextFormat(QtCore.Qt.RichText)
      layout.addWidget(self.lblMatches, 4, 1)


      self.chkDisableCrypt = QtWidgets.QCheckBox(self.tr('Disable encryption for this wallet'))
      if not noPrevEncrypt:
         self.connect(self.chkDisableCrypt, SIGNAL('toggled(bool)'), \
                      self.disablePassphraseBoxes)
         layout.addWidget(self.chkDisableCrypt, 4, 0)


      self.btnAccept = QtWidgets.QPushButton(self.tr("Accept"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      layout.addWidget(buttonBox, 5, 0, 1, 2)

      if noPrevEncrypt:
         self.setWindowTitle(self.tr("Set Encryption Passphrase"))
      else:
         self.setWindowTitle(self.tr("Change Encryption Passphrase"))

      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))

      self.setLayout(layout)

      self.connect(self.edtPasswd1, SIGNAL('textChanged(QString)'), \
                   self.checkPassphrase)
      self.connect(self.edtPasswd2, SIGNAL('textChanged(QString)'), \
                   self.checkPassphrase)

      self.connect(self.btnAccept, SIGNAL("clicked()"), \
                   self.checkPassphraseFinal)

      self.connect(self.btnCancel, SIGNAL("clicked()"), \
                    self, SLOT('reject()'))


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
         self.lblMatches.setText(self.tr('<font color=%s><b>Passphrase is non-ASCII!</b></font>' % badColor))
         return False
      if not p1 == p2:
         self.lblMatches.setText(self.tr('<font color=%s><b>Passphrases do not match!</b></font>' % badColor))
         return False
      if len(p1) < 5:
         self.lblMatches.setText(self.tr('<font color=%s><b>Passphrase is too short!</b></font>' % badColor))
         return False
      self.lblMatches.setText(self.tr('<font color=%s><b>Passphrases match!</b></font>' % goodColor))
      return True


   def checkPassphraseFinal(self):
      if self.chkDisableCrypt.isChecked():
         self.accept()
      else:
         if self.checkPassphrase():
            dlg = DlgPasswd3(self, self.main)
            if dlg.exec_():
               if not str(dlg.edtPasswd3.text()) == str(self.edtPasswd1.text()):
                  QtWidgets.QMessageBox.critical(self, self.tr('Invalid Passphrase'), \
                      self.tr('You entered your confirmation passphrase incorrectly!'), QtWidgets.QMessageBox.Ok)
               else:
                  self.accept()
            else:
               self.reject()