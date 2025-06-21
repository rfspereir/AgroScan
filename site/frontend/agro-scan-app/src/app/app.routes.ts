import { Routes } from '@angular/router';

export const routes: Routes = [
  {
    path: '',
    loadComponent: () =>
      import('./pages/login/login').then((m) => m.Login)
  },
  {
    path: 'dashboard-root',
    loadComponent: () =>
      import('./pages/dashboard-root/dashboard-root').then(
        (m) => m.DashboardRoot
      )
  },
  {
    path: 'clientes',
    loadComponent: () =>
      import('./pages/clientes/clientes').then((m) => m.Clientes)
  },
  {
    path: 'usuarios',
    loadComponent: () =>
      import('./pages/usuarios/usuarios').then((m) => m.Usuarios)
  },
  {
    path: 'PreCadastroDisp',
    loadComponent: () =>
      import('./pages/pre-cadastro-disp/pre-cadastro-disp').then((m) => m.PreCadastroDisp)
  },
  {
  path: 'dispositivos',
  loadComponent: () =>
    import('./pages/dispositivos/dispositivos').then((m) => m.Dispositivos)
  },
  {
  path: 'permissoes',
  loadComponent: () =>
    import('./pages/permissoes/permissoes').then((m) => m.Permissoes)
}


];
