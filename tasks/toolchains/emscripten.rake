MRuby::Toolchain.new(:emscripten) do |conf|
  toolchain :clang

  [conf.cc, conf.objc, conf.asm].each do |cc|
    cc.command = ENV['CC'] || 'emcc'
    cc.flags << "--valid-abspath #{MRUBY_ROOT}"
  end
  conf.cxx.command = ENV['CXX'] || 'em++'
  conf.linker.command = ENV['LD'] || 'emcc'
  conf.archiver.command = ENV['AR'] || 'emar'
  conf.exts do |exts|
    exts.object = '.bc'
    exts.executable = '.js'
    exts.library = '.a'
  end
end
