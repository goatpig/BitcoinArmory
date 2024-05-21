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
import time

from qtpy import QtCore, QtWidgets

from qtdialogs.qtdefines import STRETCH
from qtdialogs.ArmoryDialog import ArmoryDialog

from armoryengine.ArmoryUtils import PyBackgroundThread
from armoryengine.BDM import BDMPhase_Completed
from ui.QtExecuteSignal import TheSignalExecution

###############################################################################
class DlgProgress(ArmoryDialog):
   """
   Progress bar dialog. The dialog is guaranteed to be created from the main
   thread.

   The dialog is modal, meaning all other windows are barred from user
   interaction as long as this dialog is within its message loop.
   The message loop is entered either through exec_(side_thread), which will
   lock the main thread and the caller thread, and join on the side thread

   The dialog reject() signal is overloaded to render it useless. The dialog
   cannot be killed through regular means. To kill the dialog, call Kill()
   or end the side thread. Either will release the main thread. The caller
   will still join on the side thread if you only call Kill()

   To make a progress dialog that can be killed by the user (before the process
   is complete), pass a string to Interrupt. It will add a push button with
   that text, that will kill the progress dialog on click. The caller will
   still be joining on the side thread.

   Passing a string to Title will draw a title.
   Passing an integer to HBar will draw a progress bar with a Max value set to
   that integer. It can be updated through UpdateHBar(int)
   Passing a string TProgress will draw a label with that string. It can be
   updated through UpdateText(str)
   """
   def __init__(self, parent=None, main=None, Interrupt=None, HBar=None,
                Title=None, TProgress=None):

      self.running = 1
      self.status = 0
      self.main = main
      self.parent = parent
      self.Interrupt = Interrupt
      self.HBar = HBar
      self.Title = Title
      self.TProgress = None
      self.procressDone = False
      self.callbackId = None

      self.lock = threading.Lock()
      self.condVar = threading.Condition(self.lock)

      self.btnStop = None

      if main is not None:
         TheSignalExecution.executeMethod(self.setup, [parent])
      else:
         return

      while self.status == 0:
         time.sleep(0.01)

   def UpdateDlg(self, text=None, HBar=None, phase=None):
      if phase == BDMPhase_Completed:
         if self.btnStop is not None:
            self.btnStop.setText(self.tr('Close'))
         else:
            self.Kill()

      if text is not None: self.lblDesc.setText(text)
      if HBar is not None: self.hbarProgress.setValue(HBar)

   def Kill(self):
      TheSignalExecution.executeMethod(self.Exit)

   def Exit(self):
      self.running = 0
      self.main.unregisterProgressCallback(self.callbackId)
      super(ArmoryDialog, self).accept()

   def exec_(self, *args, **kwargs):
      '''
      If args[0] is a function, it will be called in exec_thread
      args[1:] is the argument list for that function
      will return the functions output in exec_thread.output, which is then
      returned by exec_
      '''
      exec_thread = PyBackgroundThread(self.exec_async, *args, **kwargs)
      exec_thread.start()

      TheSignalExecution.executeMethod(super(ArmoryDialog, self).exec_)
      exec_thread.join()

      if exec_thread.didThrowError():
         exec_thread.raiseLastError()
      else:
         return exec_thread.output

   def exec_async(self, *args, **kwargs):
      if len(args) > 0 and hasattr(args[0], '__call__'):
         func = args[0]

         try:
            rt = func(*args[1:], **kwargs)
         except Exception as e:
            self.Kill()
            raise e
            pass

         return rt

   def reject(self):
      return

   def setup(self, parent=None):
      super(DlgProgress, self).__init__(parent, self.main)

      css = """
            QtWidgets.QDialog{ border:1px solid rgb(0, 0, 0); }
            QtWidgets.QProgressBar{ text-align: center; font-weight: bold; }
            """
      self.setStyleSheet(css)

      layoutMgmt = QtWidgets.QVBoxLayout()
      self.lblDesc = QtWidgets.QLabel('')

      if self.Title is not None:
         if not self.HBar:
            self.lblTitle = QtWidgets.QLabel(self.Title)
            self.lblTitle.setAlignment(QtCore.Qt.AlignCenter)
            layoutMgmt.addWidget(self.lblTitle)


      if self.HBar is not None:
         self.hbarProgress = QtWidgets.QProgressBar(self)
         self.hbarProgress.setMaximum(self.HBar*100)
         self.hbarProgress.setMinimum(0)
         self.hbarProgress.setValue(0)
         self.hbarProgress.setMinimumWidth(250)
         layoutMgmt.addWidget(self.hbarProgress)
         self.HBarCount = 0

         if self.HBar:
            self.hbarProgress.setFormat("%s: %s%%" % (self.Title, "%p"))
      else:
         layoutMgmt.addWidget(self.lblDesc)

      if self.Interrupt is not None:
         self.btnStop = QtWidgets.QPushButton(self.Interrupt)
         self.btnStop.clicked.connect(self.Kill)

         layout_btnG = QtWidgets.QGridLayout()
         layout_btnG.setColumnStretch(0, 1)
         layout_btnG.setColumnStretch(4, 1)
         layout_btnG.addWidget(self.btnStop, 0, 1, 1, 3)
         layoutMgmt.addLayout(layout_btnG)

      self.minimize = None
      self.setWindowFlags(QtCore.Qt.Window | QtCore.Qt.FramelessWindowHint)
      self.setModal(True)

      self.setLayout(layoutMgmt)
      self.adjustSize()

      btmRight = self.parent.rect().bottomRight()
      topLeft = self.parent.rect().topLeft()
      globalbtmRight = self.parent.mapToGlobal((btmRight+topLeft)/2)

      self.move(globalbtmRight - QtCore.QPoint(self.width()/2, self.height()))
      if self.Title:
         self.setWindowTitle(self.Title)
      else:
         self.setWindowTitle(self.tr('Progress Bar'))

      self.hide()
      self.status = 1
      self.callbackId = self.main.registerProgressCallback(self)