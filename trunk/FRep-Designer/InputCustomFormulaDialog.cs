using System;

namespace FRepDesigner
{
    public partial class InputCustomFormulaDialog : Gtk.Dialog
    {
        public string Text {
            get {
                return entry1.Text;
            }
        }

        public InputCustomFormulaDialog()
        {
            this.Build();
        }
    }
}

