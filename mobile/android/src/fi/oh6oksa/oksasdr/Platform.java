package fi.oh6oksa.oksasdr;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.List;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

// Android services the shared C++ code reaches through JNI: the secure store
// (SmartLink refresh token), the share sheet (log), the multicast lock (LAN
// discovery) and the Bluetooth headset route (voice on a headset).
public final class Platform {
    private Platform() {}

    // ---- Secure store: AES-GCM with a key that never leaves the Android
    // Keystore; only the ciphertext is kept, in app-private preferences.

    private static final String KEY_ALIAS = "oksasdr_secrets";
    private static final String PREFS = "oksasdr_secure";

    private static SecretKey key() throws Exception {
        KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
        ks.load(null);
        if (ks.containsAlias(KEY_ALIAS)) {
            return ((KeyStore.SecretKeyEntry) ks.getEntry(KEY_ALIAS, null)).getSecretKey();
        }
        KeyGenerator gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        gen.init(new KeyGenParameterSpec.Builder(KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build());
        return gen.generateKey();
    }

    public static boolean saveSecret(Context context, String name, String value) {
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key());
            byte[] iv = cipher.getIV();
            byte[] data = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            byte[] blob = new byte[iv.length + data.length];
            System.arraycopy(iv, 0, blob, 0, iv.length);
            System.arraycopy(data, 0, blob, iv.length, data.length);
            prefs(context).edit().putString(name, Base64.encodeToString(blob, Base64.NO_WRAP)).apply();
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static String loadSecret(Context context, String name) {
        String stored = prefs(context).getString(name, null);
        if (stored == null) {
            return "";
        }
        try {
            byte[] blob = Base64.decode(stored, Base64.NO_WRAP);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, blob, 0, 12));
            return new String(cipher.doFinal(blob, 12, blob.length - 12), StandardCharsets.UTF_8);
        } catch (Exception e) {
            return "";
        }
    }

    public static void removeSecret(Context context, String name) {
        prefs(context).edit().remove(name).apply();
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    // ---- Share sheet

    public static boolean share(Context context, String subject, String text) {
        try {
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("text/plain");
            send.putExtra(Intent.EXTRA_SUBJECT, subject);
            send.putExtra(Intent.EXTRA_TEXT, text);
            Intent chooser = Intent.createChooser(send, subject);
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(chooser);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // ---- LAN discovery: Wi-Fi drivers may drop broadcast and multicast
    // packets unless an app holds a multicast lock.

    private static WifiManager.MulticastLock multicastLock;

    public static void setMulticast(Context context, boolean on) {
        if (on && multicastLock == null) {
            WifiManager wm = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            multicastLock = wm.createMulticastLock("OksaSDR:discovery");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        } else if (!on && multicastLock != null) {
            multicastLock.release();
            multicastLock = null;
        }
    }

    // ---- Bluetooth headset: voice (receive and microphone) through a
    // connected headset's hands-free link while a radio is connected, as the
    // iOS app does. Returns the headset's name, or "" when none is in use.

    public static String useHeadset(Context context, boolean on) {
        AudioManager am = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (!on) {
            if (Build.VERSION.SDK_INT >= 31) {
                am.clearCommunicationDevice();
            } else {
                am.stopBluetoothSco();
                am.setBluetoothScoOn(false);
            }
            am.setMode(AudioManager.MODE_NORMAL);
            return "";
        }
        if (Build.VERSION.SDK_INT >= 31) {
            List<AudioDeviceInfo> devices = am.getAvailableCommunicationDevices();
            for (AudioDeviceInfo d : devices) {
                int t = d.getType();
                if (t == AudioDeviceInfo.TYPE_BLUETOOTH_SCO || t == AudioDeviceInfo.TYPE_BLE_HEADSET) {
                    am.setMode(AudioManager.MODE_IN_COMMUNICATION);
                    if (am.setCommunicationDevice(d)) {
                        return String.valueOf(d.getProductName());
                    }
                    am.setMode(AudioManager.MODE_NORMAL);
                }
            }
            return "";
        }
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
            if (d.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO) {
                am.setMode(AudioManager.MODE_IN_COMMUNICATION);
                am.startBluetoothSco();
                am.setBluetoothScoOn(true);
                return String.valueOf(d.getProductName());
            }
        }
        return "";
    }
}
