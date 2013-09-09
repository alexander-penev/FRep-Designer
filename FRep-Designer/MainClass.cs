using System;
using Gtk;
using GLib;

namespace FRepDesigner
{
  /// <summary>
  /// Main class.
  /// </summary>
  class MainClass
  {
    public static void Main(string[] args)
    {
      Application.Init();
      MainWindow win = new MainWindow();
      win.Show();
      ExceptionManager.UnhandledException += new UnhandledExceptionHandler(OnException);
      Application.Run();
    }
    
    protected static void OnException(UnhandledExceptionArgs args)
    {
      ShowErrorDialog(args.ExceptionObject, args.IsTerminating);
      args.ExitApplication = true;
    }
    
    protected static void ShowErrorDialog(object exceptionObject, bool isTerminating)
    {
      var msgBox = new Gtk.MessageDialog(null, Gtk.DialogFlags.Modal, Gtk.MessageType.Error, Gtk.ButtonsType.Ok, exceptionObject.ToString());
      msgBox.Run();
      msgBox.Destroy(); 
    }
  }
}
