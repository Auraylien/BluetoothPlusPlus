package com.github.teamjcd.bpp;

import android.app.Dialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.text.TextUtils;
import android.util.Log;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.View.OnKeyListener;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.DialogFragment;
import androidx.fragment.app.FragmentActivity;
import androidx.preference.EditTextPreference;
import androidx.preference.Preference;
import androidx.preference.Preference.OnPreferenceChangeListener;
import androidx.preference.PreferenceFragmentCompat;

import com.github.teamjcd.bpp.db.BluetoothDeviceClassContentProvider;
import com.github.teamjcd.bpp.db.BluetoothDeviceClassData;
import com.github.teamjcd.bpp.db.BluetoothDeviceClassStore;

public class BluetoothDeviceClassEditor extends PreferenceFragmentCompat
        implements OnPreferenceChangeListener, OnKeyListener {

    public final static String URI_EXTRA = "BluetoothDeviceClassEditor.URI_EXTRA";

    private final static String TAG = BluetoothDeviceClassEditor.class.getSimpleName();

    private static final int MENU_DELETE = Menu.FIRST;
    private static final int MENU_SAVE = Menu.FIRST + 1;
    private static final int MENU_CANCEL = Menu.FIRST + 2;

    private static final String KEY_NAME = "bluetooth_device_class_name";
    private static final String KEY_CLASS = "bluetooth_device_class_class";

    private EditTextPreference mName;
    private EditTextPreference mClass;

    private BluetoothDeviceClassStore mStore;
    private BluetoothDeviceClassData mBluetoothDeviceClassData;

    private boolean mNewBluetoothDeviceClass;
    private boolean mReadOnlyBluetoothDeviceClass;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final FragmentActivity activity = getActivity();
        if (activity != null) {
            final Intent intent = activity.getIntent();
            final String action = intent.getAction();

            if (TextUtils.isEmpty(action)) {
                activity.finish();
                return;
            }

            Uri uri = null;
            if (action.equals(BluetoothDeviceClassSettings.ACTION_BLUETOOTH_DEVICE_CLASS_EDIT)) {
                uri = intent.getParcelableExtra(URI_EXTRA);
                if (!uri.isPathPrefixMatch(BluetoothDeviceClassContentProvider.DEVICE_CLASS_URI)) {
                    Log.e(TAG, "Edit request not for device class table. Uri: " + uri);
                    activity.finish();
                    return;
                }
            } else if (action.equals(BluetoothDeviceClassSettings.ACTION_BLUETOOTH_DEVICE_CLASS_INSERT)) {
                Uri insertUri = intent.getParcelableExtra(URI_EXTRA);
                if (!insertUri.isPathPrefixMatch(BluetoothDeviceClassContentProvider.DEVICE_CLASS_URI)) {
                    Log.e(TAG, "Insert request not for device class table. Uri: " + insertUri);
                    activity.finish();
                    return;
                }
                mNewBluetoothDeviceClass = true;
            } else {
                activity.finish();
                return;
            }

            mStore = BluetoothDeviceClassStore.getBluetoothDeviceClassStore(getContext());

            if (uri != null) {
                mBluetoothDeviceClassData = mStore.get(uri);
            } else {
                mBluetoothDeviceClassData = new BluetoothDeviceClassData();
            }

            mReadOnlyBluetoothDeviceClass = mBluetoothDeviceClassData.isDefault();

            if (mReadOnlyBluetoothDeviceClass) {
                mClass.setEnabled(false);
            }

            for (int i = 0; i < getPreferenceScreen().getPreferenceCount(); i++) {
                getPreferenceScreen().getPreference(i).setOnPreferenceChangeListener(this);
            }
        }
    }

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        addPreferencesFromResource(R.xml.bluetooth_device_class_editor);
        initBluetoothDeviceClassEditorUi();

        mName.setOnBindEditTextListener(editText -> {
            editText.setInputType(InputType.TYPE_TEXT_FLAG_CAP_WORDS);
            editText.setSelection(editText.getText().length());
        });
    }

    @Override
    public void onViewStateRestored(@Nullable Bundle savedInstanceState) {
        super.onViewStateRestored(savedInstanceState);
        fillUI(savedInstanceState == null);
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        preference.setSummary(newValue != null ? String.valueOf(newValue) : null);
        return true;
    }

    @Override
    public void onCreateOptionsMenu(@NonNull Menu menu, @NonNull MenuInflater inflater) {
        super.onCreateOptionsMenu(menu, inflater);

        menu.add(0, MENU_SAVE, 0, R.string.menu_save)
                .setIcon(android.R.drawable.ic_menu_save);

        menu.add(0, MENU_CANCEL, 0, R.string.menu_cancel)
                .setIcon(android.R.drawable.ic_menu_close_clear_cancel);

        if (!mNewBluetoothDeviceClass && !mReadOnlyBluetoothDeviceClass) {
            menu.add(0, MENU_DELETE, 0, R.string.menu_delete)
                    .setIcon(R.drawable.ic_delete_24);
        }
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        FragmentActivity activity = getActivity();

        switch (item.getItemId()) {
            case MENU_DELETE:
                mStore.delete(mBluetoothDeviceClassData.getId());
                if (activity != null) {
                    activity.finish();
                }
                return true;
            case MENU_SAVE:
                if (validateAndSaveBluetoothDeviceClassData()) {
                    if (activity != null) {
                        activity.finish();
                    }
                }
                return true;
            case MENU_CANCEL:
                if (activity != null) {
                    activity.finish();
                }
                return true;
            default:
                return super.onOptionsItemSelected(item);
        }
    }

    @Override
    public void onViewCreated(@NonNull View view, Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        view.setOnKeyListener(this);
        view.setFocusableInTouchMode(true);
        view.requestFocus();
    }

    @Override
    public boolean onKey(View v, int keyCode, KeyEvent event) {
        if (event.getAction() != KeyEvent.ACTION_DOWN) {
            return false;
        }

        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (validateAndSaveBluetoothDeviceClassData()) {
                FragmentActivity activity = getActivity();
                if (activity != null) {
                    activity.finish();
                }
            }

            return true;
        }

        return false;
    }

    private void initBluetoothDeviceClassEditorUi() {
        mName = findPreference(KEY_NAME);
        mClass = findPreference(KEY_CLASS);
    }

    private void fillUI(boolean firstTime) {
        if (firstTime) {
            mName.setText(mBluetoothDeviceClassData.getName());
            if (!mNewBluetoothDeviceClass) {
                mClass.setText(BluetoothDeviceClassUtils
                        .format(mBluetoothDeviceClassData.getDeviceClass()));
            }
        }

        mName.setSummary(mName.getText());
        mClass.setSummary(mClass.getText());
    }

    private boolean validateAndSaveBluetoothDeviceClassData() {
        final String errorMsg = validateBluetoothDeviceClassData();
        if (errorMsg != null) {
            showError(errorMsg);
            return false;
        }

        mBluetoothDeviceClassData.setName(mName.getText());

        if (!mReadOnlyBluetoothDeviceClass) {
            mBluetoothDeviceClassData.setDeviceClass(BluetoothDeviceClassUtils.parse(mClass.getText()));
        }

        if (mNewBluetoothDeviceClass) {
            mStore.save(mBluetoothDeviceClassData);
        } else {
            mStore.update(mBluetoothDeviceClassData);
        }

        return true;
    }

    private String validateBluetoothDeviceClassData() {
        String errMsg = null;

        final String name = mName.getText();
        final String cod = mClass.getText();

        if (TextUtils.isEmpty(name)) {
            errMsg = getResources().getString(R.string.error_name_empty);
        } else if (TextUtils.isEmpty(cod)) {
            errMsg = getResources().getString(R.string.error_device_class_empty);
        }

        if (errMsg == null) {
            try {
                BluetoothDeviceClassUtils.parse(cod);
            } catch (Exception e) {
                errMsg = getResources().getString(R.string.error_device_class_invalid);
            }
        }

        return errMsg;
    }

    private void showError(String msg) {
        ErrorDialog.showError(this, msg);
    }

    public static class ErrorDialog extends DialogFragment {
        private String msg;

        public static void showError(BluetoothDeviceClassEditor editor, String msg) {
            ErrorDialog dialog = new ErrorDialog();
            dialog.setMessage(msg);
            dialog.show(editor.getChildFragmentManager(), "error");
        }

        @Override
        @NonNull
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            //noinspection ConstantConditions
            return new AlertDialog.Builder(getContext())
                    .setTitle(R.string.error_title)
                    .setPositiveButton(android.R.string.ok, null)
                    .setMessage(msg)
                    .create();
        }

        private void setMessage(String msg) {
            this.msg = msg;
        }
    }
}
