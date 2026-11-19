package javax.security.auth.callback;

import java.io.IOException;
import java.io.InvalidObjectException;
import java.io.ObjectInputStream;

public class ChoiceCallback implements Callback, java.io.Serializable {

    private static final long serialVersionUID = -3975664071579892167L;

    private final String prompt;

    private String[] choices;

    private final int defaultChoice;

    private final boolean multipleSelectionsAllowed;

    private int[] selections;

    public ChoiceCallback(String prompt, String[] choices,
                int defaultChoice, boolean multipleSelectionsAllowed) {

        if (prompt == null || prompt.isEmpty() ||
            choices == null || choices.length == 0 ||
            defaultChoice < 0 || defaultChoice >= choices.length)
            throw new IllegalArgumentException();

        this.prompt = prompt;
        this.defaultChoice = defaultChoice;
        this.multipleSelectionsAllowed = multipleSelectionsAllowed;

        this.choices = choices.clone();
        for (int i = 0; i < choices.length; i++) {
            if (choices[i] == null || choices[i].isEmpty())
                throw new IllegalArgumentException();
        }
    }

    public String getPrompt() {
        return prompt;
    }

    public String[] getChoices() {
        return choices.clone();
    }

    public int getDefaultChoice() {
        return defaultChoice;
    }

    public boolean allowMultipleSelections() {
        return multipleSelectionsAllowed;
    }

    public void setSelectedIndex(int selection) {
        this.selections = new int[1];
        this.selections[0] = selection;
    }

    public void setSelectedIndexes(int[] selections) {
        if (!multipleSelectionsAllowed)
            throw new UnsupportedOperationException();
        this.selections = selections == null ? null : selections.clone();
    }

    public int[] getSelectedIndexes() {
        return selections == null ? null : selections.clone();
    }

    private void readObject(ObjectInputStream stream)
            throws IOException, ClassNotFoundException {
        stream.defaultReadObject();

        if ((prompt == null) || prompt.isEmpty() ||
                (choices == null) || (choices.length == 0) ||
                (defaultChoice < 0) || (defaultChoice >= choices.length)) {
            throw new InvalidObjectException(
                    "Missing/invalid prompt/choices");
        }

        choices = choices.clone();
        for (int i = 0; i < choices.length; i++) {
            if ((choices[i] == null) || choices[i].isEmpty())
                throw new InvalidObjectException("Null/empty choices");
        }

        if (selections != null) {
            selections = selections.clone();
            if (!multipleSelectionsAllowed && (selections.length != 1)) {
                throw new InvalidObjectException(
                        "Multiple selections not allowed");
            }
        }
    }
}
