using System;
using System.IO;
using System.Text.RegularExpressions;

string[] files = Directory.GetFiles("C:\\Users\\ZHT\\Desktop\\librime-1.16.1-32\\src", "*.cc", SearchOption.AllDirectories);
Regex regexNotEqual = new Regex(@"([a-zA-Z0-9_]+(?:->|\.)?[a-zA-Z0-9_]*)\.find\(([^)]+)\)\s*!=\s*\1\.end\(\)");
Regex regexEqual = new Regex(@"([a-zA-Z0-9_]+(?:->|\.)?[a-zA-Z0-9_]*)\.find\(([^)]+)\)\s*==\s*\1\.end\(\)");

int modified = 0;
foreach (string file in files) {
    string content = File.ReadAllText(file);
    string original = content;
    content = regexNotEqual.Replace(content, m => m.Groups[1].Value + ".contains(" + m.Groups[2].Value + ")");
    content = regexEqual.Replace(content, m => "!" + m.Groups[1].Value + ".contains(" + m.Groups[2].Value + ")");
    
    if (content != original) {
        File.WriteAllText(file, content);
        modified++;
        Console.WriteLine("Modified: " + file);
    }
}
Console.WriteLine($"Total files modified: {modified}");
