package;

import haxe.iterators.StringIterator;
import haxe.Json;
import haxe.io.BytesOutput;
import haxe.io.BytesData;
import haxe.io.BytesBuffer;
import haxe.io.Bytes; 

typedef SwagSection =
{
	var sectionNotes:Array<Dynamic>; // [songtime, id, len]
	var lengthInSteps:Int;
	var mustHitSection:Bool;
	var bpm:Float;
	var changeBPM:Bool;
}

typedef SwagSong =
{
	var song:String;
	var notes:Array<SwagSection>;
	var bpm:Float;

	var speed:Float;
    var stage:String;
	var player1:String;
	var player2:String; 
}

class Chart {
    public static function readFile(path:String):String {
        return sys.io.File.getContent(path);
    }

    public static function main():Void { 
        var targetSong:String = Sys.args()[0];
        var jsonContents = readFile(targetSong + '.json');

        var bytes:BytesBuffer = new BytesBuffer(); 
        function addByteString(str:String) {
            if(str.length + 1 > 32)
                throw 'naw string too long boi';
            
            bytes.addInt32(str.length);
            for(i in 0...str.length)
                bytes.addByte(str.charCodeAt(i));
        }

        var song:SwagSong = Json.parse(jsonContents).song; 
        addByteString(song.player1);
        addByteString(song.player2);
        addByteString(song.stage == null ? "stage" : song.stage);

        bytes.addFloat(song.speed);
        bytes.addInt32(song.notes.length);

        // s(p1)... s(p2)... s(stage)... speed(float) noteCount(int)
 
        var idiot:Array<Float> = [for(i in 0...8) -999999];

        var bpm:Float = song.bpm;
        for(section in song.notes) {
            var notes:Array<Dynamic> = section.sectionNotes.copy();
            notes.sort((a, b) -> a[0] - b[0]);
            
            section.sectionNotes = [];  
            for(note in notes) {
                if(note[0] - idiot[Std.int(note[1] % 8)] >= 2) {
                    section.sectionNotes.push(note);
                    idiot[Std.int(note[1] % 8)] = note[0];
                }
            }
            // removing any duplicate notes
            
            bytes.addInt32(section.lengthInSteps == null ? 15 : section.lengthInSteps); 

            if(section.changeBPM)
                bpm = section.bpm;

            bytes.addFloat(bpm);
            bytes.addByte(section.mustHitSection ? 1 : 0);
            bytes.addInt32(section.sectionNotes.length);

            for(note in section.sectionNotes) {
                var time:Float  = note[0];
                var id:Int      = Std.int(note[1] % 8); // 0 - 7
                var len:Float   = note[2];

                if(section.mustHitSection) {
                    if(id < 4)
                        id = -(id + 1);
                    else 
                        id = id - 3;
                }
                else {
                    if(id < 4)
                        id = id + 1;
                    else 
                        id = -(id - 3);
                }

                bytes.addFloat(time); 
                bytes.addInt32(id);
                bytes.addFloat(len);
            }
        } 
        // len(int) bpm(float) musthit(bool) noteCount(int) | { time(float) id(char) len(float) }

        sys.io.File.saveBytes(targetSong + '.song', bytes.getBytes());
    }
}