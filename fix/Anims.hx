package;

import haxe.io.BytesOutput;
import haxe.io.BytesData;
import haxe.io.BytesBuffer;
import haxe.io.Bytes;
import haxe.rtti.XmlParser;

typedef Animation = {
    var name:String;
    var data:Array<Int>;
}

class Anims {
    public static function readFile(path:String):String {
        return sys.io.File.getContent(path);
    }

    public static function main():Void { 
        var targetXML:String = Sys.args()[0];
        var xmlContents = readFile(targetXML + '.xml');

        var animations:Xml = Xml.parse(xmlContents);
        var bytes:BytesBuffer = new BytesBuffer();
        var anims:Array<Animation> = [];

        for(element in animations.elements()) {
            if(element.nodeName != 'TextureAtlas')
                continue;

            var currentAnimation:Null<String> = null;
            var data:Array<Int> = [];

            for(animation in element.elements()) { 
                var animationName:String = animation.get('name');
                var i:Int = animationName.length - 1;
                
                while(i >= 0) {
                    var code:Int = animationName.charCodeAt(i);
                    if(code < '0'.charCodeAt(0) || code > '9'.charCodeAt(0))
                        break;
                    i--;
                }

                animationName = animationName.substring(0, i + 1); 
                if(currentAnimation == null)
                    currentAnimation = animationName;
                else if(animationName != currentAnimation) {
                    // processed animation
					trace("added: " + currentAnimation);
                    anims.push({name: currentAnimation, data: data});

                    currentAnimation = animationName;
                    data = [];
                }

                if(i < 0)
                    throw "unexpected animation name: " + animationName;
                
                data.push(Std.parseInt( animation.get("x")));
                data.push(Std.parseInt( animation.get("y")));
                data.push(Std.parseInt( animation.get("width")));
                data.push(Std.parseInt( animation.get("height"))); 

                var temp:Null<Int> = Std.parseInt( animation.get("frameX"));
                data.push(temp == null ? 0 : temp);
                var temp:Null<Int> = Std.parseInt( animation.get("frameY"));
                data.push(temp == null ? 0 : temp);
            }
			trace("added: " + currentAnimation);
            anims.push({name: currentAnimation, data: data});
        }
        
        bytes.addInt32(anims.length);
        for(anim in anims) {
            if(anim.name.length + 1 > 32)
                throw "name too long (not really)";

            bytes.addInt32(anim.name.length);
            for(i in 0...anim.name.length)
                bytes.addByte(anim.name.charCodeAt(i));

            bytes.addInt32(anim.data.length);
            for(i in anim.data)
                bytes.addInt32(i);
        }
        sys.io.File.saveBytes(targetXML + '.animset', bytes.getBytes());
    }
}