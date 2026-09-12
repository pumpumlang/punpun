# Assertion, aggregation and benchmark helpers written in PunPun.
fn expect_int(actual: i64, expected: i64) -> void { assert(actual == expected, "expected int " + text(expected) + ", got " + text(actual)); }
fn expect_bool(actual: bool, expected: bool) -> void { assert(actual == expected, "boolean expectation failed"); }
fn expect_str(actual: String, expected: String) -> void { assert(actual == expected, "expected '"+expected+"', got '"+actual+"'"); }
fn expect_close(actual:float,expected:float,tolerance:float)->void{assert(fabs(actual-expected)<=tolerance,"float expectation failed");}

object TestSuite {
    public let name:str; public let passed:int; public let failed:int; public let failures:List<str>;
    public init(name:str){self.name=name;self.passed=0;self.failed=0;self.failures=list<str>();}
    public fn check(condition:bool,message:str)->void{if condition{self.passed=self.passed+1;}else{self.failed=self.failed+1;list_push(self.failures,message);}}
    public fn equal_int(actual:int,expected:int,message:str)->void{self.check(actual==expected,message+" expected="+text(expected)+" actual="+text(actual));}
    public fn equal_str(actual:str,expected:str,message:str)->void{self.check(actual==expected,message+" expected='"+expected+"' actual='"+actual+"'");}
    public fn equal_bool(actual:bool,expected:bool,message:str)->void{self.check(actual==expected,message);}
    public fn ok()->bool{return self.failed==0;}
    public fn summary()->str{return self.name+": "+text(self.passed)+" passed, "+text(self.failed)+" failed";}
    public fn assert_ok()->void{if !self.ok(){let mut text_value=self.summary();for failure in self.failures{text_value=text_value+"\n- "+failure;}panic(text_value);}}
}
struct Benchmark { rounds:int, total_ms:int, min_ms:int, max_ms:int, public fn average_ms()->float{if self.rounds==0{return 0.0;}return decimal(self.total_ms)/decimal(self.rounds);} }
fn benchmark(rounds:int,work:fn()->void)->Benchmark {if rounds<=0{return Benchmark(0,0,0,0);}let mut total=0;let mut minimum=9223372036854775807;let mut maximum=0;for i in 0..rounds{let start=clock_ms();work();let elapsed=clock_ms()-start;total=total+elapsed;if elapsed<minimum{minimum=elapsed;}if elapsed>maximum{maximum=elapsed;}}return Benchmark(rounds,total,minimum,maximum);}
