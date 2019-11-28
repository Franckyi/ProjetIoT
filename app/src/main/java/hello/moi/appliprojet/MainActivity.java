package hello.moi.appliprojet;

import android.os.AsyncTask;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;

public class MainActivity extends AppCompatActivity {

    private static DatagramSocket udpSocket;
    private EditText ipEditText, portEditText, formatEditText;
    private Button sendButton;
    private TextView[] textViews;
    private boolean flag;
    private int port = 10000;
    private InetAddress address;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        ipEditText = findViewById(R.id.ipEditText);
        portEditText = findViewById(R.id.portEditText);
        formatEditText = findViewById(R.id.formatEditText);
        sendButton = findViewById(R.id.sendButton);
        textViews = new TextView[]{
                findViewById(R.id.textView1),
                findViewById(R.id.textView2),
                findViewById(R.id.textView3)
        };

        try {
            udpSocket = new DatagramSocket();
        } catch (SocketException e) {
            e.printStackTrace();
        }
        new SendTask().execute();
        new ReceiverTask().execute();

        sendButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                send(formatEditText.getText().toString());
            }
        });
    }

    public void send(String message) {
        try {
            address = InetAddress.getByName(ipEditText.getText().toString());
            port = Integer.parseInt(portEditText.getText().toString());
            byte[] buf = message.getBytes();
            DatagramPacket packet = new DatagramPacket(buf, buf.length, address, port);
            udpSocket.send(packet);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private class SendTask extends AsyncTask<Void, Void, Void> {

        @Override
        protected Void doInBackground(Void... voids) {
            while (true) {
                send("getValues()");
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    private class ReceiverTask extends AsyncTask<Void, byte[], Void> {

        protected Void doInBackground(Void... rien) {
            while (true) {
                byte[] data = new byte[1024];
                DatagramPacket packet = new DatagramPacket(data, data.length);
                try {
                    udpSocket.receive(packet);
                } catch (IOException e) {
                    e.printStackTrace();
                }
                int size = packet.getLength();
                publishProgress(java.util.Arrays.copyOf(data, size));
            }
        }

        protected void onProgressUpdate(byte[]... data) {
            try {
                String str = new String(data[data.length - 1]);
                JSONObject obj = new JSONObject(str);
                String format = obj.getString("format");
                if (!flag) {
                    formatEditText.setText(format);
                    flag = true;
                }
                for (int i = 0; i < textViews.length; i++) {
                    char c = format.charAt(i);
                    String val;
                    switch (c) {
                        case 'T':
                            val = "T : " + obj.getInt("temp") / 10.0 + "°C";
                            break;
                        case 'L':
                            val = "L : " + obj.getInt("lum");
                            break;
                        case 'H':
                            val = "H : " + obj.getInt("hum") / 10.0 + "rH";
                            break;
                        default:
                            val = "?";
                    }
                    textViews[i].setText(val);
                }
            } catch (JSONException e) {
                e.printStackTrace();
            }
        }

    }
}
