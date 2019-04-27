# Bitcoin Armory Website
## Overview
This is the website for the community-developed version of [Bitcoin Armory](https://btcarmory.com/), with lead development by [goatpig](https://github.com/goatpig/BitcoinArmory). This site uses the following technologies.

- [Ruby](http://www.ruby-lang.org/en/)
- [Jekyll](https://jekyllrb.com/)
- [Bundler](https://bundler.io/)
- [github-pages](https://pages.github.com/)
- [The *jekyll-now* theme](https://github.com/barryclark/jekyll-now)
- [The *btcarmory.com* repo](https://github.com/goatpig/BitcoinArmory/tree/gh-pages)

While goatpig, the lead Armory maintainer, controls the website, users are encouraged to contribute updates to the website (e.g., tips for novice users). goatpig maintains strict control of the Armory repo but is more of a "sanity checker" for the website, simply making sure the pull requests are sensible. Website development is done primarily by [Andrew Chow](https://github.com/achow101).

## Building the website locally
The site relies on Jekyll in order to be developed locally. Ruby development can be tricky, especially cross-platform development. The following steps have been tested on Linux (Ubuntu) and macOS, and should work on Windows 10.

- *(OPTIONAL)* Clone the repo. This is necessary only if you wish to submit a PR and attempt to update the web page. (NOTE: When making a commit to your repo, you may receive an email from GitHub, stating that you don't control btcarmory.com. This is due to an issue regarding how GitHub handles the CNAME file. Just ignore the emails.)
- Install Ruby on your system. Follow the instructions [here](https://gorails.com/setup/) for your particular OS. Follow the instructions for installing any prerequisites and then follow the instructions under "Installing Ruby". You do *not* need to follow any further instructions on the site, such as installing [Ruby on Rails](http://rubyonrails.org/).
- From the command line, type `gem install jekyll jekyll-sitemap jekyll-feed bundle github-pages` in order to install various Ruby packages (gems). (Note: Some of these gems must be installed because they're listed as plugins in the \_config.yml file.)
- Check out the *gh-pages* branch in the *BitcoinArmory* repo. If developing on macOS, make sure that the osxbuild/workspace/ directory is deleted in order to prevent interference with the Jekyll binary.
- Run `jekyll serve --incremental` in order to start a web server that develops the website and auto-regenerates the site whenever changes are made. The site can be viewed with any local web browser.

Due to the site config, note that the Jekyll version must be at least 3.5.0.

## License
This website is developed under the [MIT License](https://opensource.org/licenses/MIT).

## Copyright
© 2016-2018 goatpig and Andrew Chow
