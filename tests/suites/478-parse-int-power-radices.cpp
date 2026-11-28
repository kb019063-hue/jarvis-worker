// Phase 478: parseInt rounds power-of-two radices once, not per digit.
TEST(parse_int_power_radices_round_once) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("var expected=18014398509481988;"
                   "parseInt('1000000000000000000000000000000000000000000000000000011',2)===expected && "
                   "parseInt('1000000000000000000000000003',4)===expected && "
                   "parseInt('1000000000000000003',8)===expected && "
                   "parseInt('40000000000003',16)===expected && "
                   "parseInt('g0000000003',32)===expected").to_string() == "true");
    assert(js.eval("parseInt('20000000000001',16)===9007199254740992 && "
                   "parseInt('20000000000003',16)===9007199254740996 && "
                   "parseInt('-0X40000000000003junk',16)===-18014398509481988")
               .to_string() == "true");
    assert(js.eval("1/parseInt('-0000',2)===-Infinity && isNaN(parseInt('0x',16)) && "
                   "parseInt('0x10tail',16)===16 && parseInt('z',32)!==parseInt('z',32)")
               .to_string() == "true");
    assert(js.eval("parseInt('1'+('0'.repeat(1024)),2)===Infinity && "
                   "parseInt('-1'+('0'.repeat(1024)),2)===-Infinity")
               .to_string() == "true");
}
