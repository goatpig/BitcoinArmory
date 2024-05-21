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
from qtdialogs.qtdefines import QRichLabel, GETFONT, \
   makeLayoutFrame, VERTICAL, HORIZONTAL, STRETCH
from qtdialogs.ArmoryDialog import ArmoryDialog

################################################################################
class DlgIntroMessage(ArmoryDialog):
   def __init__(self, parent=None, main=None):
      super(DlgIntroMessage, self).__init__(parent, main)

      lblInfoImg = QtWidgets.QLabel()
      lblInfoImg.setPixmap(QtGui.QPixmap('./img/MsgBox_info48.png'))
      lblInfoImg.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
      lblInfoImg.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
      lblInfoImg.setMaximumWidth(50)

      lblWelcome = QRichLabel(self.tr('<b>Welcome to Armory!</b>'))
      lblWelcome.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      lblWelcome.setFont(GETFONT('Var', 14))
      lblSlogan = QRichLabel(\
         self.tr('<i>The most advanced Bitcoin Client on Earth!</i>'))
      lblSlogan.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      lblDescr = QRichLabel(self.tr(
         '<b>You are about to use the most secure and feature-rich Bitcoin client '
         'software available!</b>  But please remember, this software '
         'is still <i>Beta</i> - Armory developers will not be held responsible '
         'for loss of bitcoins resulting from the use of this software!'
         '<br><br>'))
      lblDescr.setOpenExternalLinks(True)

      spacer = lambda: QtWidgets.QSpacerItem(20, 20, QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding)
      frmText = makeLayoutFrame(VERTICAL, [lblWelcome, spacer(), lblDescr])


      self.chkDnaaIntroDlg = QtWidgets.QCheckBox(self.tr('Do not show this window again'))

      self.requestCreate = False
      self.requestImport = False
      buttonBox = QtWidgets.QDialogButtonBox()
      frmIcon = makeLayoutFrame(VERTICAL, [lblInfoImg, STRETCH])
      frmIcon.setMaximumWidth(60)

      if len(self.main.walletMap) == 0:
         self.btnCreate = QtWidgets.QPushButton(self.tr("Create Your First Wallet!"))
         self.btnImport = QtWidgets.QPushButton(self.tr("Import Existing Wallet"))
         self.btnCancel = QtWidgets.QPushButton(self.tr("Skip"))
         self.btnCreate.clicked.connect(self.createClicked)
         self.btnImport.clicked.connect(self.importClicked)
         self.btnCancel.clicked.connect(self.reject)
         buttonBox.addButton(self.btnCreate, QtWidgets.QDialogButtonBox.AcceptRole)
         buttonBox.addButton(self.btnImport, QtWidgets.QDialogButtonBox.AcceptRole)
         buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
         self.chkDnaaIntroDlg.setVisible(False)
         frmBtn = makeLayoutFrame(HORIZONTAL, [self.chkDnaaIntroDlg, \
                                            self.btnCancel, \
                                            STRETCH, \
                                            self.btnImport, \
                                            self.btnCreate])
      else:
         self.btnOkay = QtWidgets.QPushButton(self.tr("OK!"))
         self.connect(self.btnOkay, SIGNAL(CLICKED), self.accept)
         buttonBox.addButton(self.btnOkay, QtWidgets.QDialogButtonBox.AcceptRole)
         frmBtn = makeLayoutFrame(HORIZONTAL, [self.chkDnaaIntroDlg, \
                                            STRETCH, \
                                            self.btnOkay])

      dlgLayout = QtWidgets.QGridLayout()
      dlgLayout.addWidget(frmIcon, 0, 0, 1, 1)
      dlgLayout.addWidget(frmText, 0, 1, 1, 1)
      dlgLayout.addWidget(frmBtn, 1, 0, 1, 2)

      self.setLayout(dlgLayout)
      self.setWindowTitle(self.tr('Greetings!'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))
      self.setMinimumWidth(750)


   def createClicked(self):
      self.requestCreate = True
      self.accept()

   def importClicked(self):
      self.requestImport = True
      self.accept()

   def sizeHint(self):
      return QtCore.QSize(750, 500)