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

from armoryengine.ArmoryUtils import LOGERROR
from armoryengine.Settings import TheSettings

from qtpy import QtCore, QtGui, QtWidgets
from armoryengine.ArmoryUtils import LOGERROR
from armoryengine.Settings import TheSettings
from qtdialogs.qtdefines import GETFONT, tightSizeNChar, STRETCH, \
   makeHorizFrame, makeVertFrame, QRichLabel
from qtdialogs.ArmoryDialog import ArmoryDialog

#############################################################################
class DlgEULA(ArmoryDialog):
   def __init__(self, parent=None, main=None):
      super(DlgEULA, self).__init__(parent, main)

      txtWidth, txtHeight = tightSizeNChar(self, 110)
      txtLicense = QtWidgets.QTextEdit()
      txtLicense.sizeHint = lambda: QtCore.QSize(txtWidth, 14 * txtHeight)
      txtLicense.setReadOnly(True)
      txtLicense.setCurrentFont(GETFONT('Fixed', 8))

      from LICENSE import licenseText
      txtLicense.setText(licenseText())

      self.chkAgree = QtWidgets.QCheckBox(\
        self.tr('I agree to all the terms of the license above'))

      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAccept = QtWidgets.QPushButton(self.tr("Accept"))
      self.btnAccept.setEnabled(False)
      self.btnCancel.clicked.connect(self.reject)
      self.btnAccept.clicked.connect(self.accept)
      self.chkAgree.toggled.connect(self.toggleChkBox)
      btnBox = makeHorizFrame([STRETCH, self.btnCancel, self.btnAccept])


      lblPleaseAgree = QRichLabel(self.tr(
         '<b>Armory Bitcoin Client is licensed in part under the '
         '<i>Affero General Public License, Version 3 (AGPLv3)</i> '
         'and in part under the <i>MIT License</i></b> '
         '<br><br>'
         'Additionally, as a condition of receiving this software '
         'for free, you accept all risks associated with using it '
         'and the developers of Armory will not be held liable for any '
         'loss of money or bitcoins due to software defects. '
         '<br><br>'
         '<b>Please read the full terms of the license and indicate your '
         'agreement with its terms.</b>'))


      dlgLayout = QtWidgets.QVBoxLayout()
      frmChk = makeHorizFrame([self.chkAgree, STRETCH])
      frmBtn = makeHorizFrame([STRETCH, self.btnCancel, self.btnAccept])
      frmAll = makeVertFrame([lblPleaseAgree, txtLicense, frmChk, frmBtn])

      dlgLayout.addWidget(frmAll)
      self.setLayout(dlgLayout)
      self.setWindowTitle(self.tr('Armory License Agreement'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))


   def reject(self):
      self.main.abortLoad = True
      LOGERROR('User did not accept the EULA')
      super(DlgEULA, self).reject()

   def accept(self):
      TheSettings.set('Agreed_to_EULA', True)
      super(DlgEULA, self).accept()

   def toggleChkBox(self, isEnabled):
      self.btnAccept.setEnabled(isEnabled)