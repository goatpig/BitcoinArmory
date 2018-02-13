import os
from subprocess import check_output, CalledProcessError

script_dir = os.path.dirname(os.path.realpath(__file__))
os.chdir(script_dir)
git_cmd = 'git rev-parse --short=10 HEAD'

if os.path.exists('.git') \
   and os.path.exists(os.path.join("armoryengine", "ArmoryUtils.py")):
    build = None

    # First try to determine head using Git...
    try:
        build = check_output(git_cmd.split(), shell=False)
    except OSError as err:
        print "Failed to execute '%s': %s" % (git_cmd, err.strerror)
    except CalledProcessError as err:
        print "Command '%s' failed (rc %i)" % (git_cmd, err.returncode)

    # Fallback to classic code
    if not build:
        print "Falling back to old method to determine HEAD"

        current_head = os.path.join(".git", "HEAD")
        f = open(current_head, "r")
        ref = f.read()
        f.close()
        path_parts = ref[5:-1].split("/")
        hash_loc = os.path.join(".git", *path_parts)

        if not os.path.exists(hash_loc) and len(ref.strip()) == 40:
            # HEAD is probably detached and we've got a hash already
            build = ref[:10]
        else:
            f = open(hash_loc, "r")
            build = f.read()[:10]
            f.close()

    if build:
        build_file = os.path.join("armoryengine", "ArmoryBuild.py")
        f = open(build_file, "w")
        f.write("BTCARMORY_BUILD = '%s'\n" % build)
        f.close()

        print "Build number has been updated to %s" % build
    else:
        print "Failed to determine build number!"

else:
    print "Please run this script from the root Armory source directory" \
        " along with the .git directory"

