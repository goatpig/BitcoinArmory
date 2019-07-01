##############################################################################
#                                                                            #
# Copyright (C) 2016-17, goatpig                                             #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #                                   
#                                                                            #
##############################################################################

from armoryengine.cppyyWrapper import SwigClient

class PySide_ProcessMutex(SwigClient.ProcessMutex):
   def __init__(self, port, callbck):
      if not isinstance(port, str):
         port = str(port)
      #SwigClient.ProcessMutex.__init__(self, "127.0.0.1", port)      
      
      self.callback_ = callbck
      
   def mutexCallback(self, uriLink):
      self.callback_(uriLink)
   
   