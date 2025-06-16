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
}
];
