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

from armoryengine.ArmoryUtils import MAX_COMMENT_LENGTH, isASCII
from qtdialogs.qtdefines import relaxedSizeNChar, \
   UnicodeErrorBox
from qtdialogs.ArmoryDialog import ArmoryDialog

################################################################################
class DlgSetComment(ArmoryDialog):
   """ This will be a dumb dialog for retrieving a comment from user """

   #############################################################################
   def __init__(self, parent, main, currcomment='',
      clbl = QtCore.QObject().tr("Add comment"), maxChars=MAX_COMMENT_LENGTH):
      super(DlgSetComment, self).__init__(parent, main)


      self.setWindowTitle(self.tr('Modify Comment'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))

      buttonbox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
                                   QtWidgets.QDialogButtonBox.Cancel)
      buttonbox.accepted.connect(self.accept)
      buttonbox.rejected.connect(self.reject)

      layout = QtWidgets.QGridLayout()
      lbl = QtWidgets.QLabel('%s' % clbl)
      self.edtComment = QtWidgets.QLineEdit()
      self.edtComment.setText(currcomment[:maxChars])
      h, w = relaxedSizeNChar(self, 50)
      self.edtComment.setMinimumSize(h, w)
      self.edtComment.setMaxLength(maxChars)
      layout.addWidget(lbl, 0, 0)
      layout.addWidget(self.edtComment, 1, 0)
      layout.addWidget(buttonbox, 2, 0)
      self.setLayout(layout)

   #############################################################################
   def accept(self):
      if not isASCII(self.edtComment.text()):
         UnicodeErrorBox(self)
         return
      else:
         super(DlgSetComment, self).accept()