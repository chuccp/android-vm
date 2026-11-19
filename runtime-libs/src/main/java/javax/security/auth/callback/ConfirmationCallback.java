package javax.security.auth.callback;

import java.io.IOException;
import java.io.ObjectInputStream;

public class ConfirmationCallback implements Callback, java.io.Serializable {

    private static final long serialVersionUID = -9095656433782481624L;

    public static final int UNSPECIFIED_OPTION          = -1;

    public static final int YES_NO_OPTION               = 0;

    public static final int YES_NO_CANCEL_OPTION        = 1;

    public static final int OK_CANCEL_OPTION            = 2;

    public static final int YES                         = 0;

    public static final int NO                          = 1;

    public static final int CANCEL                      = 2;

    public static final int OK                          = 3;

    public static final int INFORMATION                 = 0;

    public static final int WARNING                     = 1;

    public static final int ERROR                       = 2;

    private final String prompt;

    private final int messageType;

    private final int optionType;

    private final int defaultOption;

    private String[] options;

    private int selection;

    public ConfirmationCallback(int messageType,
                int optionType, int defaultOption) {

        if (messageType < INFORMATION || messageType > ERROR ||
            optionType < YES_NO_OPTION || optionType > OK_CANCEL_OPTION)
            throw new IllegalArgumentException();

        switch (optionType) {
        case YES_NO_OPTION:
            if (defaultOption != YES && defaultOption != NO)
                throw new IllegalArgumentException();
            break;
        case YES_NO_CANCEL_OPTION:
            if (defaultOption != YES && defaultOption != NO &&
                defaultOption != CANCEL)
                throw new IllegalArgumentException();
            break;
        case OK_CANCEL_OPTION:
            if (defaultOption != OK && defaultOption != CANCEL)
                throw new IllegalArgumentException();
            break;
        }

        this.prompt = null;
        this.messageType = messageType;
        this.optionType = optionType;
        this.options = null;
        this.defaultOption = defaultOption;
    }

    public ConfirmationCallback(int messageType,
                String[] options, int defaultOption) {

        if (messageType < INFORMATION || messageType > ERROR ||
            options == null || options.length == 0 ||
            defaultOption < 0 || defaultOption >= options.length)
            throw new IllegalArgumentException();

        this.prompt = null;
        this.messageType = messageType;
        this.optionType = UNSPECIFIED_OPTION;
        this.defaultOption = defaultOption;

        this.options = options.clone();
        for (int i = 0; i < options.length; i++) {
            if (options[i] == null || options[i].isEmpty())
                throw new IllegalArgumentException();
        }
    }

    public ConfirmationCallback(String prompt, int messageType,
                int optionType, int defaultOption) {

        if (prompt == null || prompt.isEmpty() ||
            messageType < INFORMATION || messageType > ERROR ||
            optionType < YES_NO_OPTION || optionType > OK_CANCEL_OPTION)
            throw new IllegalArgumentException();

        switch (optionType) {
        case YES_NO_OPTION:
            if (defaultOption != YES && defaultOption != NO)
                throw new IllegalArgumentException();
            break;
        case YES_NO_CANCEL_OPTION:
            if (defaultOption != YES && defaultOption != NO &&
                defaultOption != CANCEL)
                throw new IllegalArgumentException();
            break;
        case OK_CANCEL_OPTION:
            if (defaultOption != OK && defaultOption != CANCEL)
                throw new IllegalArgumentException();
            break;
        }

        this.prompt = prompt;
        this.messageType = messageType;
        this.optionType = optionType;
        this.options = null;
        this.defaultOption = defaultOption;
    }

    public ConfirmationCallback(String prompt, int messageType,
                String[] options, int defaultOption) {

        if (prompt == null || prompt.isEmpty() ||
            messageType < INFORMATION || messageType > ERROR ||
            options == null || options.length == 0 ||
            defaultOption < 0 || defaultOption >= options.length)
            throw new IllegalArgumentException();

        this.prompt = prompt;
        this.messageType = messageType;
        this.optionType = UNSPECIFIED_OPTION;
        this.defaultOption = defaultOption;

        this.options = options.clone();
        for (int i = 0; i < options.length; i++) {
            if (options[i] == null || options[i].isEmpty())
                throw new IllegalArgumentException();
        }
    }

    public String getPrompt() {
        return prompt;
    }

    public int getMessageType() {
        return messageType;
    }

    public int getOptionType() {
        return optionType;
    }

    public String[] getOptions() {
        return options == null ? null : options.clone();
    }

    public int getDefaultOption() {
        return defaultOption;
    }

    public void setSelectedIndex(int selection) {
        this.selection = selection;
    }

    public int getSelectedIndex() {
        return selection;
    }

    private void readObject(ObjectInputStream stream)
            throws IOException, ClassNotFoundException {
        stream.defaultReadObject();
        if (options != null) {
            options = options.clone();
        }
    }
}
