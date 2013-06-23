##
# De0nano
#

class De0nano < Hardware::System
  @@led     = Hardware::Register.new(0x10000100, 7, 0)
  @@pushsw  = Hardware::Register.new(0x10000110, 0, 0)
  @@dipsw   = Hardware::Register.new(0x10000120, 3, 0)

  def self.led
    @@led
  end

  def self.pushsw
    @@pushsw
  end

  def self.dipsw
    @@dipsw
  end
end

