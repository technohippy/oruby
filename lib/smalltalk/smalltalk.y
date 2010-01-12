class SmalltalkParser
rule
  program: 
           {
             result = ''
           }
         | statements
           {
             result = val.join("\n")
           }

  statements: expression '.'
              {
                result = [val[0] + ';']
              }
            | expression '.' statements
              {
                result = [val[0] + ';'] + val[2]
              }

  expression: 
              {
                result = ''
              }
            | assign
            | comparison
            | unary_message
            | binary_message
            | keyword_message
            | primitive

  assign: IDENT ':=' expression
          {
            result = "#{val[0]} = #{val[2]}"
          }

  comparison: IDENT '=' expression
              {
                result = "#{val[0]} == #{val[2]}"
              }

  unary_message: IDENT IDENT
                 {
                   result = "#{val[0]}.#{val[1]}"
                 }

  binary_message: binary_message_receiver BINARY_OPERATOR binary_message_operand
                  {
                    result = val.join(' ')
                  }

  binary_message_receiver: primitive
                         | unary_message

  binary_message_operand: primitive
                        | unary_message

  keyword_message: keyword_message_receiver arguments
                   {
                     receiver = val[0]
                     method_name = convert_to_method_name(val[1].first)
                     args = val[1].map{|a| a.last}.join(',')
                     result = "#{receiver}.#{method_name}(#{args})"
                   }

  keyword_message_receiver: primitive
                          | unary_message
                          | binary_message

  arguments: KEY keyword_message_operand
             {
               result = [[val[0].sub(':', ''), val[1]]]
             }
           | arguments KEY keyword_message_operand
             {
               result = val[0] + [[val[1].sub(':', ''), val[2]]]
             }

  keyword_message_operand: primitive
                         | unary_message
                         | binary_message

  primitive: IDENT
           | CHAR
           | BOOLEAN
           | NUMBER
           | STRING
           | '(' expression ')'
             {
               result = "(#{val[1]})"
             }
           | block

  block: '[' block_content ']'
         {
           result = "lambda{#{val[1]}}"
         }
       | '[' temporaries '|' block_content ']'
         {
           result = "lambda{|#{val[1]}|#{val[3]}}"
         }

  block_content: expression
               | statements
                 {
                   result = val.join(';')
                 }

  temporaries: 
               {
                 result = []
               }
             | temporaries ':' IDENT
               {
                 result = val[0] + [val[2]]
               }
end

---- header

class Object
  class <<self
    def subclass(name)
      self.const_set(name, Class.new(self))
    end

    def compile(code)
      signature, body = code.strip.split("\n", 2)
      pairs = signature.split(/:/)
      name = pairs.shift
      args = []
      until pairs.empty?
        args << pairs.shift
        pairs.shift # ignore
      end

      define_method(name, eval("lambda {|#{args.join(',')}| %o{ #{body} }}"))
    end
  end
end

class Proc
  def while_true(true_proc)
    true_proc.call while self.call 
  end
end

class TrueClass
  def if_true(true_proc, false_proc=lambda{})
    true_proc.call
  end

  def if_false(false_proc, true_proc=lambda{})
    true_proc.call
  end
end

class FalseClass
  def if_true(true_proc, false_proc=lambda{})
    false_proc.call
  end

  def if_false(false_proc, true_proc=lambda{})
    false_proc.call
  end
end

---- inner

def convert_to_method_name(array)
  array.first.gsub(/(.)([A-Z])/) {"#{$1}_#{$2.downcase}"}
end

def parse(str)
  str = str + '.'
  @tokens = []
  until str.empty?
    if str =~ /\A\s+/
      # ignore
    elsif str =~ /\A".*?"/m
      # ignore
    elsif str =~ /\A'.*?'/m
      @tokens.push [:STRING, $&]
    elsif str =~ /\A\$./
      @tokens.push [:CHAR, '?' + $&]
    elsif str =~ /\A(true|false)/
      @tokens.push [:BOOLEAN, $& == 'true']
    elsif str =~ /\A-?\d+\.\d+/
      @tokens.push [:NUMBER, $&.to_f]
    elsif str =~ /\A-?\d+/
      @tokens.push [:NUMBER, $&.to_i]
    elsif str =~ /\A:=/
      @tokens.push [':=', ':=']
    elsif str =~ /\A(\+|-|\/|\*|<|>|%)/
      @tokens.push [:BINARY_OPERATOR, $&]
    elsif str =~ /\A\w+:/
      @tokens.push [:KEY, $&]
    elsif str =~ /\A\w+/
      @tokens.push [:IDENT, $&]
    elsif str =~ /\A./
      @tokens.push [$&, $&]
    end
    str = $'
  end

  do_parse
end

def next_token
  @tokens.shift
end
