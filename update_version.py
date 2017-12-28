import os
import urllib2

def _fetch_url( url ):
    try:
        urllib2.urlopen(url, timeout=1)
        return True
    except urllib2.URLError as err:
        return False

script_dir = os.path.dirname(os.path.realpath(__file__))
os.chdir(script_dir)

if os.path.exists('.git'):
    if os.path.exists(os.path.join("armoryengine", "ArmoryUtils.py")):
        current_head = os.path.join(".git", "HEAD")
        f = open(current_head, "r")
        ref = f.read()
        f.close()
        path_parts = ref[5:-1].split("/")
        hash_loc = os.path.join(".git", *path_parts)
        f = open(hash_loc, "r")
        build = f.read()[:10]
        f.close()

    else:
        print "Please run this script from the root Armory source directory" \
            " along with the .git directory"
else:
    url = 'https://github.com/goatpig/BitcoinArmory.git'
    if _fetch_url( url ):
        import subprocess
        build = subprocess.check_output('git ls-remote --heads %s refs/heads/master' % url, shell=True)
        build = build[:10]
    else:
        build = 'tgz_offlne'

build_file = os.path.join("armoryengine", "ArmoryBuild.py")
f = open(build_file, "w")
f.write("BTCARMORY_BUILD = '%s'\n" % build)
f.close()

print "Build number has been updated to %s" % build
