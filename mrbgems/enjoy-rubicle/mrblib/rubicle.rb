class SecurityError < RuntimeError
end

class Rubicle
  @welcome = nil
  class << self
    attr_accessor :welcome
  end

  def initialize
    puts "Hello! I'm rubicle."
    @@welcome.call self if @@welcome
  end

  def weight
    raise SecurityError
  end
end

Carbuncle = Rubicle
