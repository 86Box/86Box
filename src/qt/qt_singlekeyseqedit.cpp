#include "qt_singlekeyseqedit.hpp"

/*
	This subclass of QKeySequenceEdit restricts the input to only a single 
	shortcut instead of an unlimited number with a fixed timeout.
*/

singleKeySequenceEdit::singleKeySequenceEdit(QWidget *parent) : QKeySequenceEdit(parent) {}

void singleKeySequenceEdit::keyPressEvent(QKeyEvent *event)
{
    QKeySequenceEdit::keyPressEvent(event);
    if (this->keySequence().count() > 0) {
        QKeySequenceEdit::setKeySequence(this->keySequence());
        
		// This could have unintended consequences since it will happen 
		// every single time the user presses a key.
        emit editingFinished();
    }
}