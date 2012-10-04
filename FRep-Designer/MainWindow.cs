using System;
using System.Collections.Generic;
using System.Drawing;

using Gtk;

namespace FRepDesigner
{

    /// <summary>
    /// Main window.
    /// </summary>
    public partial class MainWindow: Gtk.Window
    {
      public Scene Model;
      public RayTracingView View;
      
      public MainWindow(): base(Gtk.WindowType.Toplevel)
      {
        // That's a hack because of the designer. If one needs to attach an event the designer attaches
        // it in the end of the file after the call to Initialize. Works for the most of the events
        // but not for events like Realize which happen in the initialization. This function is used
        // to attach the event handlers before the initialization part.
        PreBuild();
        Build();
      }

      protected virtual void PreBuild ()
      {
        this.Realized += new global::System.EventHandler(this.OnRealized);
      }

      protected void OnRealized(object sender, System.EventArgs e)
      {
        // Init enviroment
        Model = new Scene();
        View = new RayTracingView(Model, image1.Pixbuf);
        Model.Changed += SceneChanged;
      }

      protected void OnDeleteEvent(object sender, DeleteEventArgs a)
      {
        Application.Quit();
        a.RetVal = true;
      }

      protected void OnExitActionActivated(object sender, System.EventArgs e) {
        Application.Quit();
      }

      protected void OnOpenActionActivated (object sender, System.EventArgs e)
      {
            var fc = new Gtk.FileChooserDialog ("Choose the file to open",
                                            this, Gtk.FileChooserAction.Open,
                                            "Cancel", Gtk.ResponseType.Cancel,
                                            "Open", Gtk.ResponseType.Accept);
            try {
                fc.SelectMultiple = true;
                fc.SetCurrentFolder (Environment.CurrentDirectory);
                if (fc.Run () == (int)Gtk.ResponseType.Accept) {
                    List<string> filesToLoad = new List<string> ();
                    filesToLoad.AddRange (fc.Filenames);
                    foreach (string fileName in fc.Filenames) {
                        ShowMessage(String.Format ("Loadding {0}...", fileName));
                    }
                }
            } finally {
              fc.Destroy();
            }
      }

      private void ShowMessage(string msg)
      {
        var msgBox = new Gtk.MessageDialog(null, Gtk.DialogFlags.Modal, Gtk.MessageType.Info,
                                           Gtk.ButtonsType.Ok, msg);
        msgBox.Run();
        msgBox.Destroy();
      }

      private void SceneChanged(Scene scene, EventArgs e)
      {
        QueueDraw();
      }
      
      protected void OnSpehereActionActivated(object sender, System.EventArgs e)
      {
        Model.Solids.Add(new FRepSolid("x*x+y*y+z*z-1"));
        Model.OnChanged(EventArgs.Empty);
      }
      
    }

}