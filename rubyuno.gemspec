# -*- encoding: utf-8 -*-
require File.expand_path('../lib/rubyuno/version', __FILE__)

Gem::Specification.new do |gem|
  gem.authors       = ["Daniel Vandersluis", "Tsutomu Uchino"]
  gem.email         = ["daniel@codexed.com", "hanya.runo@gmail.com"]
  gem.description   = %q{Ruby-OpenOffice/LibreOffice UNO native bridge}
  gem.summary       = %q{rubyuno is a Ruby-UNO (Universal Network Object) bridge, used to interact with OpenOffice and LibreOffice}
  gem.homepage      = "http://www.github.com/dvandersluis/rubyuno"

  gem.files         = `git ls-files`.split($\)
  gem.extensions    = ['ext/rubyuno/extconf.rb']
  gem.test_files    = gem.files.grep(%r{^(test|spec|features)/})
  gem.name          = "rubyuno"
  gem.require_paths = ["lib"]
  gem.version       = Rubyuno::VERSION
end
