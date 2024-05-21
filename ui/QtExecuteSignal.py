##############################################################################
#                                                                            #
# Copyright (C) 2017, goatpig                                                #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from threading import Thread
from time import sleep
from qtpy import QtCore

from armoryengine.ArmoryUtils import LOGERROR

################################################################################
class QtExecuteSignalError(Exception):
   pass

##############################################################################
class QtExecuteSignal(QtCore.QObject):
   executeSignal = QtCore.Signal(list)

   ###########################################################################
   def __init__(self):
      super(QtExecuteSignal, self).__init__()
      self.executeSignal.connect(self.methodSlot)
      self.waiting = {}

   ###########################################################################
   def executeMethod(self, _callable, *args):
      if not callable(_callable):
         print (f"** executeMethod: {str(_callable)} **")
         print (f"** args type: {str(type(args))}, args: {str(args)} **")
         raise QtExecuteSignalError("invalid arguments")

      self.executeSignal.emit([{
         'callable' : _callable,
         'args' : args
      }])

   ###########################################################################
   def methodSlot(self, execList):
      execList[0]['callable'](*(execList[0]['args']))

   ###########################################################################
   def callLater(self, delay, _callable, *_args):
      #if a given method is already waiting on delayed execution, update the
      #args and return
      if _callable in self.waiting:
         self.waiting[_callable] = _args
         return

      self.waiting[_callable] = _args
      thr = Thread(target=self.callLaterThread, args=(delay, _callable) + _args)
      thr.start()

   ###########################################################################
   def callLaterThread(self, delay, _callable, *args):
      sleep(delay)
      args = self.waiting.pop(_callable, [])
      self.executeMethod(_callable, *args)

TheSignalExecution = QtExecuteSignal()