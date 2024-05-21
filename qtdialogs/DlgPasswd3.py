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

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.qtdefines import QRichLabel, MIN_PASSWD_WIDTH

class DlgPasswd3(ArmoryDialog):
   def __init__(self, parent=None, main=None):
      super(DlgPasswd3, self).__init__(parent, main)


      lblWarnImgL = QtWidgets.QLabel()
      lblWarnImgL.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImgL.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      lblWarnTxt1 = QRichLabel(\
         self.tr('<font color="red"><b>!!! DO NOT FORGET YOUR PASSPHRASE !!!</b></font>'), size=4)
      lblWarnTxt1.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      lblWarnTxt2 = QRichLabel(self.tr(
        '<b>No one can help you recover you bitcoins if you forget the '
         'passphrase and don\'t have a paper backup!</b> Your wallet and '
         'any <u>digital</u> backups are useless if you forget it.  '
         '<br><br>'
         'A <u>paper</u> backup protects your wallet forever, against '
         'hard-drive loss and losing your passphrase.  It also protects you '
         'from theft, if the wallet was encrypted and the paper backup '
         'was not stolen with it.  Please make a paper backup and keep it in '
         'a safe place.'
         '<br><br>'
         '<b>Please enter your passphrase a third time to indicate that you '
         'are aware of the risks of losing your passphrase!</b>'), doWrap=True)


      self.edtPasswd3 = QtWidgets.QLineEdit()
      self.edtPasswd3.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd3.setMinimumWidth(MIN_PASSWD_WIDTH(self))

      bbox = QtWidgets.QDialogButtonBox()
      btnOk = QtWidgets.QPushButton(self.tr('Accept'))
      btnNo = QtWidgets.QPushButton(self.tr('Cancel'))
      btnOk.clicked.connect(self.accept)
      btnNo.clicked.connect(self.reject)
      bbox.addButton(btnOk, QtWidgets.QDialogButtonBox.AcceptRole)
      bbox.addButton(btnNo, QtWidgets.QDialogButtonBox.RejectRole)
      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblWarnImgL, 0, 0, 4, 1)
      layout.addWidget(lblWarnTxt1, 0, 1, 1, 1)
      layout.addWidget(lblWarnTxt2, 2, 1, 1, 1)
      layout.addWidget(self.edtPasswd3, 5, 1, 1, 1)
      layout.addWidget(bbox, 6, 1, 1, 2)
      self.setLayout(layout)
      self.setWindowTitle(self.tr('WARNING!'))