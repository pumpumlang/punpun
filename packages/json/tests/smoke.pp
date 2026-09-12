import src.main
launch {let parsed=json_decode("{\"x\":7,\"a\":[true,null]}");assert(parsed.ok(),parsed.error);assert(parsed.value.get_int("x",0)==7,"json");assert(json_valid(json_encode(parsed.value)),"encode");say("json-ok");}
