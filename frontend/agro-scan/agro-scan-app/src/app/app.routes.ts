import { Routes } from '@angular/router';
import { LoginComponent } from './pages/login/login';

export const routes: Routes = [
  {
    path: '',
    loadComponent: () => import('./pages/login/login').then(m => m.LoginComponent)
  }
];
