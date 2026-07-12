#ifndef TOKEN_H
#define TOKEN_H

enum TokenType{
    TOKEN_LEFTPAREN,
    TOKEN_RIGHTPAREN,
    TOKEN_TOKENSTR,
    TOKEN_LOGICAL,
    TOKEN_RELATIONAL,
};

class Token
{
public:
    Token() = default;
    virtual ~Token() = default;
    virtual TokenType token_type() const{ return TOKEN_TOKENSTR;}
    virtual string token_string() const{ return "";}
    virtual void print(ostream&) const{}
    virtual int precedence() const{ return -1;}
    friend ostream& operator<<(ostream& outs, const Token& token){
        token.print(outs);
        return outs;
    }
    friend ostream& operator<<(ostream& outs, const Token* token){
        token->print(outs);
        return outs;
    }
};

#endif
