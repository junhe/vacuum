import java.text.BreakIterator;
import java.util.Locale;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.io.IOException;
import java.io.BufferedReader;
import java.io.FileReader;
public class sentencebreaker
{
    public static void main(String[] args) throws Exception {
        //read in file
        //String str = "how soft works?Java!Python";
        BufferedReader brTest = new BufferedReader(new FileReader("/mnt/ssd/wiki/sample_docs"));
        String str = null;
        while ((str = brTest.readLine())!=null) {
        //System.out.println("=============");
        int last_offset = str.length();

        BreakIterator itor = BreakIterator.getSentenceInstance(Locale.ENGLISH);
        itor.setText(str);
        int start = itor.first();
        for (int end = itor.next();
                end != BreakIterator.DONE;
                start = end, end = itor.next()) {
            if (end == last_offset) {
                System.out.print(start + "," + end + ";");
            } else {
                System.out.print(start + "," + (end-1) + ";");
            }
            //System.out.print("\t*" + str.substring(start, end) + "*\n");
        }
        System.out.print("\n");
        }
    }
}
