TEST(vm_nullish_computed_writes_do_not_convert_the_key) {
    TestPage page;
    auto check = [&](const char* src, const char* exp){ assert(page.js.eval(src).to_string()==exp); };
    check("var s='';var k={toString:function(){s+='c';return 'x'}};try{null[k]=1}catch(e){s+=e.name}String(s)", "TypeError");
}
