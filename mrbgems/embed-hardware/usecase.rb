
module Hardware
  class De0nano < System
    register :led,    0x10000100, 7, 0
    register :pushsw, 0x10000110, 0, 0
    register :dipsw,  0x10000120, 3, 0
  end
end

sys = Hardware::De0nano
sys.led.clear
i = 0
while(true)
  sys.led[i].toggle
  sys.sleep(100)
  i = (i + 1) & 7
end

