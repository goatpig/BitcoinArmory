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
from qtdialogs.qtdefines import MSGBOX, tightSizeNChar

################################################################################
# The optionalMsg argument is not word wrapped so the caller is responsible for limiting
# the length of the longest line in the optionalMsg
def MsgBoxCustom(wtype, title, msg, wCancel=False, yesStr=None, noStr=None,
                                                     optionalMsg=None):
   """
   Creates a message box with custom button text and icon
   """

   class dlgWarn(ArmoryDialog):
      def __init__(self, dtype, dtitle, wmsg, withCancel=False, yesStr=None, noStr=None):
         super(dlgWarn, self).__init__(None)

         msgIcon = QtWidgets.QLabel()
         fpix = ''
         if dtype==MSGBOX.Good:
            fpix = ':/MsgBox_good48.png'
         if dtype==MSGBOX.Info:
            fpix = ':/MsgBox_info48.png'
         if dtype==MSGBOX.Question:
            fpix = ':/MsgBox_question64.png'
         if dtype==MSGBOX.Warning:
            fpix = ':/MsgBox_warning48.png'
         if dtype==MSGBOX.Critical:
            fpix = ':/MsgBox_critical64.png'
         if dtype==MSGBOX.Error:
            fpix = ':/MsgBox_error64.png'


         if len(fpix)>0:
            msgIcon.setPixmap(QtGui.QPixmap(fpix))
            msgIcon.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)

         lblMsg = QtWidgets.QLabel(msg)
         lblMsg.setTextFormat(QtCore.Qt.RichText)
         lblMsg.setWordWrap(True)
         lblMsg.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         lblMsg.setOpenExternalLinks(True)
         w,h = tightSizeNChar(lblMsg, 70)
         lblMsg.setMinimumSize( w, 3.2*h )
         buttonbox = QtWidgets.QDialogButtonBox()

         if dtype==MSGBOX.Question:
            if not yesStr: yesStr = self.tr('&Yes')
            if not noStr:  noStr = self.tr('&No')
            btnYes = QtWidgets.QPushButton(yesStr)
            btnNo  = QtWidgets.QPushButton(noStr)
            btnYes.clicked.connect(self.accept)
            btnNo.clicked.connect(self.reject)
            buttonbox.addButton(btnYes,QtWidgets.QDialogButtonBox.AcceptRole)
            buttonbox.addButton(btnNo, QtWidgets.QDialogButtonBox.RejectRole)
         else:
            cancelStr = self.tr('&Cancel') if (noStr is not None or withCancel) else ''
            yesStr    = self.tr('&OK') if (yesStr is None) else yesStr
            btnOk     = QtWidgets.QPushButton(yesStr)
            btnCancel = QtWidgets.QPushButton(cancelStr)
            btnOk.clicked.connect(self.accept)
            btnCancel.clicked.connect(self.reject)
            buttonbox.addButton(btnOk, QtWidgets.QDialogButtonBox.AcceptRole)
            if cancelStr:
               buttonbox.addButton(btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

         spacer = QtWidgets.QSpacerItem(20, 10, QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding)

         layout = QtWidgets.QGridLayout()
         layout.addItem(  spacer,         0,0, 1,2)
         layout.addWidget(msgIcon,        1,0, 1,1)
         layout.addWidget(lblMsg,         1,1, 1,1)
         if optionalMsg:
            optionalTextLabel = QtWidgets.QLabel(optionalMsg)
            optionalTextLabel.setTextFormat(QtCore.Qt.RichText)
            optionalTextLabel.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            w,h = tightSizeNChar(optionalTextLabel, 70)
            optionalTextLabel.setMinimumSize( w, 3.2*h )
            layout.addWidget(optionalTextLabel, 2,0,1,2)
         layout.addWidget(buttonbox, 3,0, 1,2)
         layout.setSpacing(20)
         self.setLayout(layout)
         self.setWindowTitle(dtitle)

   dlg = dlgWarn(wtype, title, msg, wCancel, yesStr, noStr)
   result = dlg.exec_()

   return result