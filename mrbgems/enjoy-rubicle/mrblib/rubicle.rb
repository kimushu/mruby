class SecurityError < RuntimeError
end

class Rubicle
  def initialize
    puts "Hello! I'm rubicle."
  end

  def weight
    raise SecurityError
  end
end

Carbuncle = Rubicle
