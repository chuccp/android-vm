package javax.security.auth.callback;

import java.lang.ref.Cleaner;
import java.util.Arrays;

import jdk.internal.ref.CleanerFactory;

import java.io.IOException;
import java.io.InvalidObjectException;
import java.io.ObjectInputStream;

public class PasswordCallback implements Callback, java.io.Serializable {

    private static final long serialVersionUID = 2267422647454909926L;

    private transient Cleaner.Cleanable cleanable;

    private final String prompt;

    private final boolean echoOn;

    private char[] inputPassword;

    public PasswordCallback(String prompt, boolean echoOn) {
        if (prompt == null || prompt.isEmpty())
            throw new IllegalArgumentException();

        this.prompt = prompt;
        this.echoOn = echoOn;
    }

    public String getPrompt() {
        return prompt;
    }

    public boolean isEchoOn() {
        return echoOn;
    }

    public void setPassword(char[] password) {

        if (cleanable != null) {
            cleanable.clean();
            cleanable = null;
        }

        this.inputPassword = (password == null ? null : password.clone());

        if (this.inputPassword != null) {
            cleanable = CleanerFactory.cleaner().register(
                    this, cleanerFor(inputPassword));
        }
    }

    public char[] getPassword() {
        return (inputPassword == null ? null : inputPassword.clone());
    }

    public void clearPassword() {

        if (cleanable != null) {
            cleanable.clean();
            cleanable = null;
        }
    }

    private static Runnable cleanerFor(char[] password) {
        return () -> {
            Arrays.fill(password, ' ');
        };
    }

    private void readObject(ObjectInputStream stream)
            throws IOException, ClassNotFoundException {
        stream.defaultReadObject();

        if (prompt == null || prompt.isEmpty()) {
            throw new InvalidObjectException("Missing prompt");
        }

        if (inputPassword != null) {
            inputPassword = inputPassword.clone();
            cleanable = CleanerFactory.cleaner().register(
                    this, cleanerFor(inputPassword));
        }
    }
}
