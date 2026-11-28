// Phase 462, finding 462: Reflect.set follows the supplied receiver for data writes.
TEST(reflect_set_defines_data_properties_on_the_receiver) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // A writable own data property is written on the receiver, not the target.
    assert(run("(function(){var target={x:1}, receiver={};"
               "var ok=Reflect.set(target,'x',2,receiver);"
               "return ok+','+target.x+','+receiver.x+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'x');})()") == "true,1,2,true");

    // When the target and its prototype chain have no descriptor, CreateDataProperty
    // makes a new, ordinary own data property on the receiver.
    assert(run("(function(){var target={}, receiver={};"
               "var ok=Reflect.set(target,'x',7,receiver), d=Object.getOwnPropertyDescriptor(receiver,'x');"
               "return ok+','+String(target.x)+','+receiver.x+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'x')+','+"
               "d.writable+','+d.enumerable+','+d.configurable;})()") == "true,undefined,7,true,true,true,true");

    // Inherited writable data properties also write the receiver; inherited
    // non-writable data properties refuse before the receiver is changed.
    assert(run("(function(){var proto={x:1}, target=Object.create(proto), receiver={};"
               "var ok=Reflect.set(target,'x',2,receiver);"
               "return ok+','+target.x+','+proto.x+','+receiver.x;})()") == "true,1,1,2");
    assert(run("(function(){var proto={}; Object.defineProperty(proto,'x',{value:1});"
               "var target=Object.create(proto), receiver={};"
               "var ok=Reflect.set(target,'x',2,receiver);"
               "return ok+','+target.x+','+Object.prototype.hasOwnProperty.call(receiver,'x');})()")
           == "false,1,false");

    // The receiver's own descriptor is checked, not its prototype: a
    // non-writable data property or an accessor refuses without calling its setter.
    assert(run("(function(){var target={x:1}, receiver={};"
               "Object.defineProperty(receiver,'x',{value:3,writable:false});"
               "var ok=Reflect.set(target,'x',2,receiver);"
               "return ok+','+target.x+','+receiver.x;})()") == "false,1,3");
    assert(run("(function(){var writes=0, target={x:1}, receiver={};"
               "Object.defineProperty(receiver,'x',{get:function(){return 4;},"
               "set:function(){writes++;}});"
               "var ok=Reflect.set(target,'x',2,receiver);"
               "return ok+','+target.x+','+receiver.x+','+writes;})()") == "false,1,4,0");
    // A writable own data property on the receiver is updated in place and
    // retains its existing attributes.
    assert(run("(function(){var target={x:1}, receiver={};"
               "Object.defineProperty(receiver,'x',{value:3,writable:true,enumerable:false});"
               "var ok=Reflect.set(target,'x',2,receiver), d=Object.getOwnPropertyDescriptor(receiver,'x');"
               "return ok+','+receiver.x+','+d.writable+','+d.enumerable+','+d.configurable;})()")
           == "true,2,true,false,false");

    // CreateDataProperty ignores inherited setters and data properties on the
    // receiver: the new own property shadows either one.
    assert(run("(function(){var target={x:1}, receiver=Object.create({});"
               "var calls=0; Object.defineProperty(Object.getPrototypeOf(receiver),'x',"
               "{set:function(){calls++;}});"
               "var ok=Reflect.set(target,'x',2,receiver);"
               "return ok+','+receiver.x+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'x')+','+calls;})()")
           == "true,2,true,0");
    assert(run("(function(){var target={x:1}, receiver=Object.create({x:3});"
               "var ok=Reflect.set(target,'x',2,receiver);"
               "return ok+','+receiver.x+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'x');})()") == "true,2,true");
    // An inherited setter on the target still runs with the receiver as this.
    assert(run("(function(){var target=Object.create({set x(v){this.seen=v;}}), receiver={};"
               "var ok=Reflect.set(target,'x',8,receiver);"
               "return ok+','+receiver.seen+','+String(target.seen);})()") == "true,8,undefined");
    assert(run("(function(){var target={x:1}, receiver={}; Object.preventExtensions(receiver);"
               "return Reflect.set(target,'x',2,receiver)+','+target.x+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'x');})()") == "false,1,false");

    // A non-extensible target with no property does not prevent a distinct,
    // extensible receiver from receiving the new property.
    assert(run("(function(){var target=Object.preventExtensions(Object.create(null)), receiver={};"
               "var ok=Reflect.set(target,'x',5,receiver);"
               "return ok+','+receiver.x+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'x');})()") == "true,5,true");

    // Explicit undefined is a receiver, not the omitted-argument default.
    assert(run("(function(){var target={x:1};"
               "return Reflect.set(target,'x',2,undefined)+','+target.x;})()") == "false,1");

    // An array receiver gets a real own index, even when its prototype has a
    // setter for that key. The target array's own index is likewise untouched.
    assert(run("(function(){var target=[1], receiver=[];"
               "var ok=Reflect.set(target,'0',9,receiver);"
               "return ok+','+target[0]+','+receiver[0]+','+receiver.length;})()") == "true,1,9,1");
    assert(run("(function(){var target={'0':1}, receiver=[], calls=0, proto={};"
               "Object.defineProperty(proto,'0',{set:function(){calls++;}});"
               "Object.setPrototypeOf(receiver,proto);"
               "var ok=Reflect.set(target,'0',9,receiver);"
               "return ok+','+receiver[0]+','+receiver.length+','+calls+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'0');})()") == "true,9,1,0,true");

    // Typed-array element descriptors are writable data properties, but a
    // distinct receiver gets the write without touching the backing element.
    assert(run("(function(){var target=new Uint8Array([1]), receiver={};"
               "var ok=Reflect.set(target,'0',7,receiver), d=Object.getOwnPropertyDescriptor(receiver,'0');"
               "return ok+','+target[0]+','+receiver[0]+','+d.writable+','+d.enumerable+','+d.configurable;})()")
           == "true,1,7,true,true,true");
    // Preserve integer-indexed exotic behavior for a canonical index beyond
    // the view: it is handled, but does not create an ordinary receiver key.
    assert(run("(function(){var target=new Uint8Array([1]), receiver={};"
               "var ok=Reflect.set(target,'1',7,receiver);"
               "return ok+','+target[0]+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'1');})()") == "true,1,false");
    // A non-canonical numeric-looking name is an ordinary property, not an
    // integer-indexed typed-array element.
    assert(run("(function(){var target=new Uint8Array([1]), receiver={};"
               "var ok=Reflect.set(target,'01',7,receiver);"
               "return ok+','+target[0]+','+receiver['01']+','+"
               "Object.prototype.hasOwnProperty.call(receiver,'01');})()") == "true,1,7,true");

    // Sparse array indices live in the map past the dense vector; they remain
    // own writable data properties when they are used as a receiver.
    assert(run("(function(){var target={}, receiver=[]; receiver[70000]=3;"
               "var ok=Reflect.set(target,'70000',8,receiver), d=Object.getOwnPropertyDescriptor(receiver,'70000');"
               "return ok+','+receiver[70000]+','+receiver.length+','+d.writable+','+d.enumerable+','+d.configurable;})()")
           == "true,8,70001,true,true,true");

    // A symbol remains a symbol when the new own property is created.
    assert(run("(function(){var key=Symbol('receiver'), target={}, receiver={};"
               "var ok=Reflect.set(target,key,11,receiver), keys=Reflect.ownKeys(receiver);"
               "return ok+','+receiver[key]+','+(keys[0]===key);})()") == "true,11,true");
}
