import src.main
launch {expect_equal_i64(2+2,4,"math");let suite=TestSuite("package");suite.check(true,"true");suite.assert_ok();say("testing-ok");}
