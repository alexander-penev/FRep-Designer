using System;
using Gtk;

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
      Application.Run();
    }
  }
}
