#!/usr/bin/env python3
# Copyright (c) 2017 Jonas Schnelli
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import subprocess

class SPVToolTest (BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-debug=net']]

    def setup_network(self, split=False):
        self.nodes = start_nodes(1, self.options.tmpdir, self.extra_args[:3])
        self.is_network_split=False
        self.sync_all()
        cur_time = int(time.time())- 100*600
        for i in range(100):
            self.nodes[0].setmocktime(cur_time + 600)
            self.nodes[0].generate(1)
            cur_time += 600

        self.nodes[0].setmocktime(cur_time + 1600)
    
    def execute_and_get_response(self, cmd):
        dummyfile = self.options.tmpdir + "/dummy"
        try:
            os.remove(dummyfile)
        except OSError:
            pass
        proc = subprocess.call(cmd+" > dummy", shell=True)
        with open("dummy") as f:
            data=f.read().replace('\n', '')
        try:
            os.remove(dummyfile)
        except OSError:
            pass
        return data
        
    def run_test (self):
        max_size = 1000
        log_stdout = tempfile.SpooledTemporaryFile(max_size=2**16)
        log_stderr = tempfile.SpooledTemporaryFile(max_size=2**16)
        
        #sync with no headers database (-f 0) and debug (-d) only against localhost
        cmd = "./bitcoin-spv --regtest -f 0 -d -i 127.0.0.1:"+str(p2p_port(0))+" scan"
        data = self.execute_and_get_response(cmd)
        assert("parsing 1 tx(s) from block at height: 100" in data)
        
        # do the same with a headers db
        try:
            os.remove("headers.db")
        except OSError:
            pass
        cmd = "./bitcoin-spv --regtest -d -i 127.0.0.1:"+str(p2p_port(0))+" scan"
        data = self.execute_and_get_response(cmd)
        assert("parsing 1 tx(s) from block at height: 100" in data)
        try:
            os.remove("headers.db")
        except OSError:
            pass

if __name__ == '__main__':
    SPVToolTest().main()
