class DE0
  @hexled = Hardware::Register.new(0x10000200, 30, 0)
  @led    = Hardware::Register.new(0x10000210, 9, 0)
  @sw     = Hardware::Register.new(0x10000220, 9, 0)
  @button = Hardware::Register.new(0x10000230, 2, 1)
  class << self
    attr_reader :hexled, :led, :sw, :button

    def lchika(t = 0)
      case(t)
      when 0
        puts <<EOD
DE0.led.clear
i = 0
loop {
  DE0.led[i].toggle
  Kernel.msleep 200
  i += 1
  i = 0 if i > 9
} =>
EOD
        DE0.led.clear; i = 0
        loop { DE0.led[i].toggle; Kernel.msleep 200; i += 1; i = 0 if i > 9 }
      when 1
        puts <<EOD
DE0.led.clear
5.times {|n| DE0.led[n*2].set }
loop {
  DE0.led.toggle
  Kernel.msleep 300
}
EOD
        DE0.led.clear; 5.times {|n| DE0.led[n*2].set }
        loop { DE0.led.toggle; Kernel.msleep 300 }
      end
    end
  end

end
